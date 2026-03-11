#!/usr/bin/env node
/**
 * postinstall.js — macOS-only post-install tasks
 *
 * 1. Ad-hoc re-sign the .app bundle so macOS accepts UNUserNotificationCenter
 *    requests after the bundle was extracted from an npm tarball.
 * 2. Register the bundle with Launch Services so the notification icon
 *    appears correctly.
 *
 * All failures are non-fatal: the script always exits 0 so that
 * `npm install` never fails because of these optional steps.
 */

'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

if (process.platform !== 'darwin') {
  process.exit(0);
}

// ── Locate the app bundle ─────────────────────────────────────────────────────

const pkgName = process.arch === 'arm64'
  ? '@cc-notify/darwin-arm64'
  : '@cc-notify/darwin-x64';

let appBundle;
try {
  const pkgDir = path.dirname(
    require.resolve(pkgName + '/package.json', {
      paths: [__dirname, path.join(__dirname, '..'), path.join(__dirname, '../..')],
    })
  );
  appBundle = path.join(pkgDir, 'bin', 'cc-notify.app');
} catch (_) {
  // Platform package not installed (e.g. CI environment); skip silently.
  process.exit(0);
}

if (!fs.existsSync(appBundle)) {
  process.exit(0);
}

// ── Ad-hoc code sign ─────────────────────────────────────────────────────────

const signResult = spawnSync(
  'codesign',
  ['--force', '--deep', '--sign', '-', appBundle],
  { stdio: 'pipe' }
);

if (signResult.error || signResult.status !== 0) {
  // codesign unavailable or failed — non-fatal.
}

// ── Register with Launch Services ────────────────────────────────────────────

const lsregister =
  '/System/Library/Frameworks/CoreServices.framework' +
  '/Frameworks/LaunchServices.framework/Support/lsregister';

if (fs.existsSync(lsregister)) {
  spawnSync(lsregister, ['-f', appBundle], { stdio: 'pipe' });
}

process.exit(0);
