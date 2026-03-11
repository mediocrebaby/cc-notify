#!/usr/bin/env node
/**
 * cc-notify — JS wrapper for the native binary
 *
 * Detects the current platform, resolves the path to the pre-compiled
 * binary from the matching optional dependency package, and runs it
 * synchronously with all arguments and stdin forwarded.
 *
 * All error paths exit 0 so as never to block a Claude Code Stop hook.
 */

'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

// ── Platform → package name mapping ──────────────────────────────────────────

const PLATFORM_MAP = {
  'darwin-arm64': '@cc-notify/darwin-arm64',
  'darwin-x64':   '@cc-notify/darwin-x64',
  'linux-x64':    '@cc-notify/linux-x64',
  'win32-x64':    '@cc-notify/win32-x64',
};

const platformKey = `${process.platform}-${process.arch}`;
const pkgName = PLATFORM_MAP[platformKey];

if (!pkgName) {
  // Unsupported platform — exit silently so Stop hook is never blocked.
  process.exit(0);
}

// ── Resolve binary path inside the platform package ──────────────────────────

function resolveBinary() {
  let pkgDir;
  try {
    // Resolve the platform package relative to this script's location so it
    // works regardless of where npm installed the global package.
    pkgDir = path.dirname(
      require.resolve(pkgName + '/package.json', {
        paths: [__dirname, path.join(__dirname, '..'), path.join(__dirname, '../..')],
      })
    );
  } catch (_) {
    return null;
  }

  const binDir = path.join(pkgDir, 'bin');

  if (process.platform === 'darwin') {
    // macOS: binary lives inside the .app bundle
    const exe = path.join(binDir, 'cc-notify.app', 'Contents', 'MacOS', 'cc-notify');
    return fs.existsSync(exe) ? exe : null;
  }

  if (process.platform === 'win32') {
    const exe = path.join(binDir, 'cc-notify.exe');
    return fs.existsSync(exe) ? exe : null;
  }

  // Linux
  const exe = path.join(binDir, 'cc-notify');
  return fs.existsSync(exe) ? exe : null;
}

const binaryPath = resolveBinary();

if (!binaryPath) {
  // Platform package installed but binary missing — exit silently.
  process.exit(0);
}

// ── Read stdin when not a TTY (hook mode) ─────────────────────────────────────

let stdinData = null;
if (!process.stdin.isTTY) {
  try {
    // fd 0 = stdin; readFileSync on fd 0 drains it synchronously.
    stdinData = fs.readFileSync(0);
  } catch (_) {
    // stdin may not be readable on some platforms; ignore.
  }
}

// ── Spawn the native binary ───────────────────────────────────────────────────

const result = spawnSync(binaryPath, process.argv.slice(2), {
  input: stdinData !== null ? stdinData : undefined,
  stdio: stdinData !== null ? ['pipe', 'inherit', 'inherit'] : 'inherit',
  // On Windows, passing the app bundle is handled by the binary itself.
  windowsHide: true,
});

// Mirror the binary's exit code, but always succeed (0) on spawn errors
// to avoid blocking the Claude Code Stop hook.
if (result.error) {
  process.exit(0);
}

process.exit(result.status !== null ? result.status : 0);
