// Linux / WSL notification.
//
// Linux:  libnotify C API  → org.freedesktop.Notifications D-Bus service
// WSL:    libnotify first; if no display server is reachable, falls back to
//         calling the Windows build (cc-notify.exe) via WSL interop — that
//         binary uses C++/WinRT natively, no PowerShell involved.

#include "notifier.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <cstdlib>

#include <libnotify/notify.h>

// ── helpers ──────────────────────────────────────────────────────────────────

static bool isWSL() {
    std::ifstream f("/proc/version");
    if (!f.is_open()) return false;
    std::string line;
    std::getline(f, line);
    std::string low(line);
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    return low.find("microsoft") != std::string::npos;
}

// Escape for POSIX shell double-quoted string.
static std::string shEscape(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') r += '\\';
        else if (c == '\n' || c == '\r')                   { r += ' '; continue; }
        r += c;
    }
    return r;
}

// ── libnotify ────────────────────────────────────────────────────────────────

static bool notifyViaLibnotify(const std::string& title,
                               const std::string& message) {
    if (!notify_is_initted())
        if (!notify_init("cc-notify")) return false;

    NotifyNotification* n = notify_notification_new(
        title.c_str(), message.c_str(), nullptr);
    if (!n) return false;

    GError* err = nullptr;
    const bool ok = notify_notification_show(n, &err) != FALSE;
    if (err) g_error_free(err);
    g_object_unref(n);
    return ok;
}

// ── WSL Windows interop ──────────────────────────────────────────────────────
// Call our own Windows build (cc-notify.exe) via the WSL interop layer.
// That binary uses C++/WinRT directly — no PowerShell, no scripts.

static bool notifyViaWslInterop(const std::string& title,
                                const std::string& message) {
    // cc-notify.exe must be installed on the Windows side (e.g. C:\Windows or
    // somewhere on $PATH in Windows).  Pass title/message as CLI arguments.
    const std::string cmd =
        "cc-notify.exe"
        " --title \""   + shEscape(title)   + "\""
        " --message \"" + shEscape(message) + "\""
        " > /dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

// ── public entry point ───────────────────────────────────────────────────────

bool sendNotification(const std::string& title, const std::string& message) {
    // Always try libnotify first (works on Linux desktops and WSLg).
    if (notifyViaLibnotify(title, message)) return true;

    // On WSL without a display, libnotify fails — escalate to Windows toast.
    if (isWSL()) return notifyViaWslInterop(title, message);

    return false;
}
