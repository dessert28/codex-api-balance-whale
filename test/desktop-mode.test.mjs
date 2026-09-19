import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { desktopWorkArea, shouldShowDesktopWidget } = require('../desktop/desktop-mode.cjs');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

test('desktop widget remains visible while Codex is alive even when its window is minimized', () => {
  assert.equal(shouldShowDesktopWidget({ rendererReady: true, host: { hostAlive: true, visible: false, attached: false }, manuallyHidden: false }), true);
  assert.equal(shouldShowDesktopWidget({ rendererReady: true, host: { hostAlive: false }, manuallyHidden: false }), false);
  assert.equal(shouldShowDesktopWidget({ rendererReady: true, host: { hostAlive: true }, manuallyHidden: true }), false);
});

test('desktop widget uses the primary display work area instead of Codex bounds', () => {
  const workArea = { x: 0, y: 0, width: 1920, height: 1040 };
  assert.deepEqual(desktopWorkArea({ getPrimaryDisplay: () => ({ workArea }) }), workArea);
});

test('desktop mode disables native attachment and enables floating overlay behavior', async () => {
  const main = await fs.readFile(path.join(root, 'desktop', 'main.cjs'), 'utf8');
  const supervisor = await fs.readFile(path.join(root, 'desktop', 'supervisor.ps1'), 'utf8');

  assert.match(main, /window\.setAlwaysOnTop\(true, 'floating'\)/);
  assert.match(main, /desktopMode: true/);
  assert.doesNotMatch(supervisor, /\[WhaleWindows\]::Attach\(/);
  assert.doesNotMatch(supervisor, /\[WhaleWindows\]::IsFollowing\(/);
  assert.doesNotMatch(supervisor, /nativeFollowing/);
});
