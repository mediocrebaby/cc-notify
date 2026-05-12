/**
 * cc-notify — Claude Code task completion notifier
 *
 * Reads a Claude Code Stop-hook JSON payload from stdin and fires a
 * platform-native OS notification.
 *
 * Hook configuration  (~/.claude/settings.json):
 *   {
 *     "hooks": {
 *       "Stop": [{
 *         "hooks": [{"type": "command", "command": "cc-notify"}]
 *       }]
 *     }
 *   }
 *
 * Notification back-ends (no scripts, no interpreters):
 *   Windows  — C++/WinRT  Windows.UI.Notifications
 *   macOS    — Objective-C++  UserNotifications.framework
 *   Linux    — libnotify C API  (→ D-Bus org.freedesktop.Notifications)
 *   WSL      — libnotify first; falls back to cc-notify.exe via WSL interop
 */

#include "notifier.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// ============================================================
// Platform detection
// ============================================================

enum class Platform { Windows, WSL, macOS, Linux, Unknown };

static Platform detectPlatform() {
#if defined(_WIN32)
    return Platform::Windows;
#elif defined(__APPLE__)
    return Platform::macOS;
#elif defined(__linux__)
    std::ifstream ver("/proc/version");
    if (ver.is_open()) {
        std::string line;
        std::getline(ver, line);
        std::string low(line);
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (low.find("microsoft") != std::string::npos) return Platform::WSL;
    }
    return Platform::Linux;
#else
    return Platform::Unknown;
#endif
}

static const char* platformName(Platform p) {
    switch (p) {
        case Platform::Windows: return "Windows";
        case Platform::WSL:     return "WSL";
        case Platform::macOS:   return "macOS";
        case Platform::Linux:   return "Linux";
        default:                return "Unknown";
    }
}

// ============================================================
// Minimal JSON field extractor  (no external dependencies)
// ============================================================

static std::string jsonGetString(const std::string& json,
                                 const std::string& key) {
    const std::string needle = '"' + key + '"';
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();

    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' ||
            json[pos] == '\n' || json[pos] == '\r' || json[pos] == ':'))
        ++pos;

    if (pos >= json.size() || json[pos] != '"') return {};
    ++pos;

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case 'r':  result += '\r'; break;
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                default:   result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}

static bool jsonGetBool(const std::string& json,
                        const std::string& key,
                        bool defaultVal = false) {
    const std::string needle = '"' + key + '"';
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;
    pos += needle.size();

    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' ||
            json[pos] == '\n' || json[pos] == '\r' || json[pos] == ':'))
        ++pos;

    if (pos + 3 < json.size() && json.substr(pos, 4) == "true")  return true;
    if (pos + 4 < json.size() && json.substr(pos, 5) == "false") return false;
    return defaultVal;
}

// ============================================================
// String utilities
// ============================================================

static std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\n\r") - a + 1);
}

static std::string firstLine(const std::string& s) {
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        const std::string t = trim(line);
        if (!t.empty()) return t;
    }
    return trim(s);
}

static std::string truncate(const std::string& s, size_t maxLen = 120) {
    if (s.size() <= maxLen) return s;
    size_t cut = maxLen - 3;
    while (cut > 0 && s[cut] != ' ') --cut;
    if (cut == 0) cut = maxLen - 3;
    return s.substr(0, cut) + "...";
}

// ============================================================
// Main
// ============================================================

static void printUsage(const char* prog) {
    std::cout <<
        "Usage: " << prog << " [OPTIONS]\n"
        "\n"
        "Reads a Claude Code Stop hook JSON payload from stdin and sends a\n"
        "native OS notification. No scripts or interpreters are invoked.\n"
        "\n"
        "Options:\n"
        "  --test               Send a test notification and exit\n"
        "  --title <text>       Notification title  (skips stdin)\n"
        "  --message <text>     Notification body   (skips stdin)\n"
        "  --platform           Print detected platform name and exit\n"
        "  --version            Print version and exit\n"
        "  --help               Print this help and exit\n"
        "\n"
        "Notification back-ends:\n"
        "  Windows  C++/WinRT  Windows.UI.Notifications\n"
        "  macOS    UserNotifications.framework  (Objective-C++)\n"
        "  Linux    libnotify  (D-Bus org.freedesktop.Notifications)\n"
        "  WSL      libnotify first, then cc-notify.exe via WSL interop\n";
}

int main(int argc, char* argv[]) {
    std::string cliTitle, cliMessage;
    bool testMode = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h")    { printUsage(argv[0]); return 0; }
        if (arg == "--version" || arg == "-v") { std::cout << "cc-notify 1.0.0\n"; return 0; }
        if (arg == "--platform")               { std::cout << platformName(detectPlatform()) << "\n"; return 0; }
        if (arg == "--test")                   { testMode = true; }

        if ((arg == "--title" || arg == "--message") && i + 1 < argc) {
            const std::string val = argv[++i];
            if (arg == "--title")   cliTitle   = val;
            else                    cliMessage = val;
        }
    }

    // ── Test mode ────────────────────────────────────────────────────────────
    if (testMode) {
        const Platform p = detectPlatform();
        std::cerr << "cc-notify: testing on " << platformName(p) << " ...\n";
        const bool ok = sendNotification(
            "Claude Code",
            "Test notification — cc-notify is working correctly!");
        std::cerr << (ok ? "cc-notify: OK\n" : "cc-notify: FAILED\n");
        return ok ? 0 : 1;
    }

    // ── Explicit --title / --message mode ────────────────────────────────────
    if (!cliTitle.empty() || !cliMessage.empty()) {
        sendNotification(
            cliTitle.empty()   ? "Claude Code"     : cliTitle,
            cliMessage.empty() ? "Task completed." : cliMessage);
        return 0;
    }

    // ── Hook mode: read JSON from stdin ──────────────────────────────────────
    std::string json;
    {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        json = ss.str();
    }

    // Guard: if stop_hook_active is true, Claude Code re-triggered this hook
    // after a previous run.  Exit 0 immediately to allow it to stop.
    if (jsonGetBool(json, "stop_hook_active"))
        return 0;

    // Build notification content from hook payload.
    std::string title   = "Claude Code";
    std::string message = "Task completed.";

    const std::string cwd     = jsonGetString(json, "cwd");
    const std::string lastMsg = jsonGetString(json, "last_assistant_message");

    if (!cwd.empty()) {
        const size_t sep = cwd.find_last_of("/\\");
        const std::string dir =
            (sep != std::string::npos) ? cwd.substr(sep + 1) : cwd;
        if (!dir.empty() && dir != "." && dir != "~")
            title = "Claude Code - " + dir;
    }

    if (!lastMsg.empty()) {
        const std::string summary = trim(firstLine(lastMsg));
        if (!summary.empty())
            message = truncate(summary);
    }

    sendNotification(title, message);

    // Always exit 0 — never block Claude Code from stopping.
    return 0;
}
