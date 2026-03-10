# cc-notify

A native OS notification tool for [Claude Code](https://claude.ai/code) task completion. Hooks into the Claude Code `Stop` event and fires a system notification — no scripts, no interpreters, no external dependencies.

## Features

- **Truly native**: uses OS-level APIs directly — WinRT on Windows, `UserNotifications.framework` on macOS, libnotify on Linux
- **Zero dependencies**: no Python, no PowerShell, no shell scripts — a single compiled binary
- **WSL-aware**: tries libnotify first; falls back to the Windows build via WSL interop when no display server is available
- **Infinite-loop safe**: detects `stop_hook_active` and exits immediately to avoid re-triggering itself
- **Always exits 0**: never blocks Claude Code from stopping

## Notification Backends

| Platform | API |
|----------|-----|
| Windows  | C++/WinRT `Windows.UI.Notifications` |
| macOS    | Objective-C++ `UserNotifications.framework` |
| Linux    | libnotify → D-Bus `org.freedesktop.Notifications` |
| WSL      | libnotify first, then `cc-notify.exe` via WSL interop |

## Requirements

| Platform | Requirements |
|----------|-------------|
| Windows  | Windows 10 1903+ · MSVC 2019+ · Windows SDK 10.0.18362+ |
| macOS    | macOS 10.14 Mojave+ · Xcode 12+ |
| Linux    | GCC 8+ or Clang 7+ · libnotify (`libnotify-dev`) · pkg-config |
| WSL      | Same as Linux; optionally `cc-notify.exe` on the Windows `PATH` for headless use |

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The compiled binary is placed at `build/Release/cc-notify` (or `cc-notify.exe` on Windows).

### Linux: install libnotify

```bash
# Debian / Ubuntu
sudo apt install libnotify-dev pkg-config

# Arch
sudo pacman -S libnotify pkgconf

# Fedora / RHEL
sudo dnf install libnotify-devel pkgconf
```

## Installation

Copy the binary to any directory on your `PATH`:

```bash
# Linux / macOS
sudo cmake --install build --config Release
# or manually:
cp build/Release/cc-notify /usr/local/bin/

# Windows — copy cc-notify.exe (and optionally icon.ico / icon.png) to:
# C:\Windows\  or any directory in your system PATH
```

> **Windows icon**: place `icon.ico` (taskbar icon) and/or `icon.png` (in-notification image) next to `cc-notify.exe`. On first run the binary auto-registers the `"Claude Code"` AUMID and creates a Start Menu shortcut — no admin rights required.

## Hook Configuration

Add the following to `~/.claude/settings.json`:

```json
{
  "hooks": {
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "cc-notify"
          }
        ]
      }
    ]
  }
}
```

Claude Code will pipe a JSON payload to `cc-notify` on stdin whenever a task finishes. The notification title shows the working directory name and the body shows the first line of Claude's last message (truncated to 120 characters).

## Usage

```
cc-notify [OPTIONS]

Options:
  --test               Send a test notification and exit
  --title <text>       Notification title  (skips stdin)
  --message <text>     Notification body   (skips stdin)
  --platform           Print detected platform name and exit
  --version            Print version and exit
  --help               Print this help and exit
```

### Verify the installation

```bash
cc-notify --test
```

### Send a custom notification

```bash
cc-notify --title "Build finished" --message "All tests passed."
```

### Print detected platform

```bash
cc-notify --platform
# Windows | macOS | Linux | WSL
```

## How It Works

When Claude Code finishes a task it runs the `Stop` hook and writes a JSON payload to the process stdin:

```json
{
  "cwd": "/home/user/my-project",
  "last_assistant_message": "Done! I've updated all three files.",
  "stop_hook_active": false
}
```

`cc-notify` extracts the `cwd` and `last_assistant_message` fields and sends a native notification:

- **Title**: `Claude Code - <directory name>`
- **Body**: first non-empty line of `last_assistant_message`, truncated to 120 chars

## Building on Windows (detailed)

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload and the **Windows 11 SDK**.
2. Open a **Developer Command Prompt** or **Developer PowerShell**.
3. Run:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
.\build\Release\cc-notify.exe --test
```

The binary is statically linked against the CRT — it runs without any VCRUNTIME redistributable.

## License

MIT — see [LICENSE](LICENSE).
