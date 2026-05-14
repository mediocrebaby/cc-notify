#!/usr/bin/env node
/**
 * postinstall.js — per-platform post-install tasks
 *
 * macOS:
 *   1. Ad-hoc re-sign the .app bundle so macOS accepts UNUserNotificationCenter
 *      requests after the bundle was extracted from an npm tarball.
 *   2. Register the bundle with Launch Services so the notification icon
 *      appears correctly.
 *
 * Windows (global installs only):
 *   Place cc-notify.exe (and its icon sidecars) into the npm global bin
 *   directory, which is already on PATH. The native exe is otherwise buried
 *   inside node_modules\cc-notify-windows-x64\bin and unreachable by name —
 *   putting it on PATH lets the WSL build's interop fallback find and run
 *   `cc-notify.exe`, so WSL can emit native Windows notifications.
 *
 * All failures are non-fatal: the script always exits 0 so that
 * `npm install` never fails because of these optional steps.
 */

'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

// ── Resolve a platform package's bin directory ───────────────────────────────

function platformBinDir(pkgName) {
  try {
    const pkgDir = path.dirname(
      require.resolve(pkgName + '/package.json', {
        paths: [__dirname, path.join(__dirname, '..'), path.join(__dirname, '../..')],
      })
    );
    return path.join(pkgDir, 'bin');
  } catch (_) {
    // Platform package not installed (e.g. CI environment) — skip silently.
    return null;
  }
}

// ── macOS ────────────────────────────────────────────────────────────────────

function setupMacOS() {
  const binDir = platformBinDir(
    process.arch === 'arm64' ? 'cc-notify-darwin-arm64' : 'cc-notify-darwin-x64'
  );
  if (!binDir) return;

  const appBundle = path.join(binDir, 'cc-notify.app');
  if (!fs.existsSync(appBundle)) return;

  // Ad-hoc code sign — codesign being unavailable or failing is non-fatal.
  spawnSync('codesign', ['--force', '--deep', '--sign', '-', appBundle], {
    stdio: 'pipe',
  });

  // Register with Launch Services so the notification icon resolves.
  const lsregister =
    '/System/Library/Frameworks/CoreServices.framework' +
    '/Frameworks/LaunchServices.framework/Support/lsregister';
  if (fs.existsSync(lsregister)) {
    spawnSync(lsregister, ['-f', appBundle], { stdio: 'pipe' });
  }
}

// ── Windows ──────────────────────────────────────────────────────────────────

// Make `src` reachable at `dest`: prefer a symlink, fall back to a copy.
// Windows symlinks need admin rights or Developer Mode, so the copy fallback
// is the common case — either way `cc-notify.exe` ends up callable by name.
function place(src, dest) {
  try {
    if (fs.existsSync(dest)) fs.unlinkSync(dest);
  } catch (_) {
    // Couldn't clear an existing entry — the symlink/copy below may still work.
  }
  try {
    fs.symlinkSync(src, dest, 'file');
    return 'linked';
  } catch (_) {
    // No symlink privilege — fall through to a plain copy.
  }
  try {
    fs.copyFileSync(src, dest);
    return 'copied';
  } catch (_) {
    return null;
  }
}

function setupWindows() {
  // Only meaningful for `npm install -g`: a local install is never on PATH.
  if (process.env.npm_config_global !== 'true') return;

  const binDir = platformBinDir('cc-notify-windows-x64');
  if (!binDir) return;

  const exe = path.join(binDir, 'cc-notify.exe');
  if (!fs.existsSync(exe)) return;

  // On Windows the npm global "bin" directory is the prefix itself — it holds
  // the .cmd shims and is on PATH. npm exports its config to lifecycle scripts;
  // fall back to deriving the prefix from this package's own location:
  //   <prefix>\node_modules\cc-notify\scripts  ->  <prefix>
  const prefix =
    process.env.npm_config_prefix || path.resolve(__dirname, '..', '..', '..');

  if (
    !prefix ||
    !fs.existsSync(prefix) ||
    path.resolve(prefix) === path.resolve(binDir)
  ) {
    return;
  }

  // Place the exe plus any icon sidecars it looks for next to itself.
  const exeResult = place(exe, path.join(prefix, 'cc-notify.exe'));
  for (const icon of ['icon.ico', 'icon.png']) {
    const iconSrc = path.join(binDir, icon);
    if (fs.existsSync(iconSrc)) place(iconSrc, path.join(prefix, icon));
  }

  if (exeResult) {
    console.log(
      `cc-notify: ${exeResult} cc-notify.exe into ${prefix} (on PATH) — ` +
        'WSL can now fall back to it for native Windows notifications.'
    );
  } else {
    console.log(
      'cc-notify: could not place cc-notify.exe on PATH automatically.\n' +
        '  For WSL notifications, copy it from\n    ' +
        exe +
        '\n  into any directory on your Windows PATH.'
    );
  }
}

// ── Dispatch ─────────────────────────────────────────────────────────────────

try {
  if (process.platform === 'darwin') {
    setupMacOS();
  } else if (process.platform === 'win32') {
    setupWindows();
  }
} catch (_) {
  // Never fail `npm install` over an optional post-install step.
}

process.exit(0);
