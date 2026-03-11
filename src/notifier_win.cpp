// Windows notification via C++/WinRT (Windows.UI.Notifications).
// No PowerShell, no scripts — pure WinRT API calls.
//
// Application identity: "Claude Code" is registered as a custom AUMID under
// HKCU\SOFTWARE\Classes\AppUserModelId\Claude Code on first run.
// No administrator privileges required.

#include "notifier.h"

#include <string>

#include <windows.h>

#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>

#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "shell32.lib")

using namespace winrt::Windows::Data::Xml::Dom;
using namespace winrt::Windows::UI::Notifications;

// The AppUserModelID we register and use.
static constexpr wchar_t kAumid[] = L"cc-notify";
static constexpr wchar_t kRegKeyPath[] =
    L"SOFTWARE\\Classes\\AppUserModelId\\cc-notify";

// Return directory containing the running executable (with trailing backslash).
static std::wstring exeDir() {
  wchar_t buf[MAX_PATH] = {};
  if (!::GetModuleFileNameW(nullptr, buf, MAX_PATH))
    return {};
  std::wstring path(buf);
  const auto slash = path.find_last_of(L"\\/");
  return (slash != std::wstring::npos) ? path.substr(0, slash + 1)
                                       : std::wstring{};
}

// Return full path to a file next to the exe if it exists, otherwise empty.
static std::wstring sidecarPath(const wchar_t *filename) {
  const std::wstring p = exeDir() + filename;
  return (::GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
             ? p
             : std::wstring{};
}

// Convert UTF-8 to UTF-16 for WinRT string APIs.
static std::wstring toWide(const std::string &utf8) {
  if (utf8.empty())
    return {};
  const int len =
      ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (len <= 0)
    return {};
  std::wstring ws(static_cast<size_t>(len - 1), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, ws.data(), len);
  return ws;
}

// Create (or update) a Start Menu shortcut for "Claude Code" with our AUMID.
// Windows notification system uses the shortcut's icon as the small app icon
// shown in the notification header — this is the documented approach for Win32
// apps (same as Slack, Teams, etc.). Requires COM to already be initialized.
// Idempotent: recreates the shortcut only if icon.ico has changed.
static void ensureStartMenuShortcut() {
  // Locate %APPDATA%\Microsoft\Windows\Start Menu\Programs
  wchar_t appdataPath[MAX_PATH] = {};
  if (FAILED(
          ::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdataPath)))
    return;
  const std::wstring lnkPath =
      std::wstring(appdataPath) +
      L"\\Microsoft\\Windows\\Start Menu\\Programs\\cc-notify.lnk";

  // Get exe path and icon.ico path.
  wchar_t exePath[MAX_PATH] = {};
  if (!::GetModuleFileNameW(nullptr, exePath, MAX_PATH))
    return;
  const std::wstring icoPath = sidecarPath(L"icon.ico");

  // If the shortcut already exists, only recreate it when icon.ico is present
  // and the shortcut's icon field is out of date (simple heuristic: skip if
  // shortcut exists and we have no new icon to set).
  const bool shortcutExists =
      (::GetFileAttributesW(lnkPath.c_str()) != INVALID_FILE_ATTRIBUTES);
  if (shortcutExists && icoPath.empty())
    return;

  IShellLinkW *pLink = nullptr;
  if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW,
                                reinterpret_cast<void **>(&pLink))))
    return;

  pLink->SetPath(exePath);
  pLink->SetDescription(L"Claude Code task notifier");

  // Set icon: prefer icon.ico next to exe, fall back to the exe itself.
  if (!icoPath.empty())
    pLink->SetIconLocation(icoPath.c_str(), 0);
  else
    pLink->SetIconLocation(exePath, 0);

  // Embed the AUMID in the shortcut so Windows links it to our notifications.
  IPropertyStore *pStore = nullptr;
  if (SUCCEEDED(pLink->QueryInterface(IID_IPropertyStore,
                                      reinterpret_cast<void **>(&pStore)))) {
    PROPVARIANT pv;
    ::InitPropVariantFromString(kAumid, &pv);
    pStore->SetValue(PKEY_AppUserModel_ID, pv);
    ::PropVariantClear(&pv);
    pStore->Commit();
    pStore->Release();
  }

  // Save the .lnk file.
  IPersistFile *pFile = nullptr;
  if (SUCCEEDED(pLink->QueryInterface(IID_IPersistFile,
                                      reinterpret_cast<void **>(&pFile)))) {
    pFile->Save(lnkPath.c_str(), TRUE);
    pFile->Release();
  }

  pLink->Release();
}

// Register "Claude Code" as a recognised AUMID in the current user's registry.
// Windows requires the AUMID to have a registry entry under
//   HKCU\SOFTWARE\Classes\AppUserModelId\<aumid>
// with at minimum a DisplayName value so the notification service will accept
// it. No elevation needed; idempotent (safe to call every run).
static void ensureAumidRegistered() {
  HKEY hKey = nullptr;
  const LONG rc = ::RegCreateKeyExW(HKEY_CURRENT_USER, kRegKeyPath, 0, nullptr,
                                    REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                                    nullptr, &hKey, nullptr);
  if (rc != ERROR_SUCCESS)
    return;

  // DisplayName  — shown in Windows Settings > Notifications.
  static constexpr wchar_t kDisplayName[] = L"cc-notify";
  ::RegSetValueExW(
      hKey, L"DisplayName", 0, REG_SZ,
      reinterpret_cast<const BYTE *>(kDisplayName),
      static_cast<DWORD>((::wcslen(kDisplayName) + 1) * sizeof(wchar_t)));

  ::RegCloseKey(hKey);
}

bool sendNotification(const std::string &title, const std::string &message) {
  try {
    // Initialize a multi-threaded COM apartment first (needed by both
    // IShellLink and WinRT).
    // RPC_E_CHANGED_MODE means COM is already initialised — that's fine.
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error &e) {
      if (e.code() != static_cast<winrt::hresult>(RPC_E_CHANGED_MODE))
        throw;
    }

    // Ensure registry entry and Start Menu shortcut exist.
    ensureAumidRegistered();
    ensureStartMenuShortcut();

    // Build the ToastGeneric XML template.
    // appLogoOverride shows icon.png (if present) inside the notification
    // bubble.
    const std::wstring pngPath = sidecarPath(L"icon.png");
    const std::wstring logoXml =
        pngPath.empty()
            ? std::wstring{}
            : (L"      <image placement=\"appLogoOverride\" src=\"file:///" +
               pngPath + L"\"/>");

    XmlDocument xml;
    xml.LoadXml(L"<toast>"
                L"  <visual>"
                L"    <binding template=\"ToastGeneric\">"
                L"      <text/>" // [0] title
                L"      <text/>" // [1] body
                + logoXml +
                L"    </binding>"
                L"  </visual>"
                L"</toast>");

    const auto texts = xml.GetElementsByTagName(L"text");
    texts.GetAt(0).InnerText(toWide(title));
    texts.GetAt(1).InnerText(toWide(message));

    // Use our own registered AUMID — "Claude Code".
    ToastNotificationManager::CreateToastNotifier(kAumid).Show(
        ToastNotification(xml));

    return true;
  } catch (...) {
    return false;
  }
}
