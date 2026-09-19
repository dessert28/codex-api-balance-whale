import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { quotaBubbleModules } from '../desktop/ui/quota-bubble.mjs';
import { createDispatcher } from '../runtime/dispatcher.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

test('renders five-hour then weekly quota with reset countdowns', () => {
  const now = Date.parse('2026-09-19T00:00:00.000Z');
  const modules = quotaBubbleModules({
    windows: {
      fiveHour: { usedPercent: 12, resetsAt: now + 125 * 60 * 1000 },
      weekly: { usedPercent: 34, resetsAt: now + 73 * 60 * 60 * 1000 },
    },
  }, { now });

  assert.deepEqual(modules.map(item => item.text), [
    '5 小时：已用 12% · 2小时5分钟后重置',
    '本周：已用 34% · 3天1小时后重置',
  ]);
  assert.equal(modules.some(item => /今日|token/.test(item.text)), false);
});

test('marks stale cached data and keeps unavailable windows explicit', () => {
  const modules = quotaBubbleModules({
    stale: true,
    windows: { fiveHour: null, weekly: { usedPercent: 0, resetsAt: null } },
  }, { now: Date.parse('2026-09-19T00:00:00.000Z') });

  assert.deepEqual(modules.map(item => item.text), [
    '5 小时：暂无数据（上次数据）',
    '本周：已用 0% · 重置时间未知（上次数据）',
  ]);
});

test('renders two unavailable lines when no snapshot can be read', () => {
  const modules = quotaBubbleModules(null, { now: Date.parse('2026-09-19T00:00:00.000Z') });

  assert.deepEqual(modules.map(item => item.text), [
    '5 小时：暂无数据',
    '本周：暂无数据',
  ]);
});

test('desktop page loads the shared quota formatter before the widget', async () => {
  const html = await fs.readFile(path.join(root, 'desktop', 'ui', 'widget.html'), 'utf8');
  const dispatcher = await fs.readFile(path.join(root, 'runtime', 'dispatcher.mjs'), 'utf8');

  assert.match(html, /<script type="module" src="\/quota-bubble\.mjs"><\/script>.*<script src="\/dsh-whale\/widget\.js" defer><\/script>/s);
  assert.match(dispatcher, /'\/quota-bubble\.mjs': 'quota-bubble\.mjs'/);
});

test('whale clicks invoke the manual quota bubble and do not schedule token summaries', async () => {
  const widget = await fs.readFile(path.join(root, 'assets', 'whale-widget.js'), 'utf8');

  assert.match(widget, /function showCodexQuotaOnClick\(\)/);
  assert.match(widget, /if \(clickAllowed && !drag\.moved\) \{\s*showCodexQuotaOnClick\(\);\s*return;/s);
  assert.doesNotMatch(widget, /showCodexUsageSummary/);
});

test('clicking the manual quota bubble advances to the configured random bubble step', async () => {
  const widget = await fs.readFile(path.join(root, 'assets', 'whale-widget.js'), 'utf8');

  assert.match(widget, /manualQuota: true/);
  assert.match(widget, /bubbleScene\.kind === 'alert' && whaleSysItem && whaleSysItem\.manualQuota\) \{\s*showRandomBubbleAfterQuota\(\);\s*return;/s);
  assert.match(widget, /function showRandomBubbleAfterQuota\(\) \{\s*hideBubble\(\);\s*if \(!bubbleOn\) return;\s*bubbleRoundOn = true;\s*bubbleSeqIdx = 1;\s*bubbleShowSeqNext\(\);/s);
});

test('dispatcher serves the quota module with a JavaScript MIME type', async () => {
  const dataDir = await fs.mkdtemp(path.join(root, '.tmp-quota-bubble-'));
  const dispatcher = createDispatcher({ dataDir, monitor: false, autoRefresh: false });
  try {
    const response = await dispatcher.dispatch('/quota-bubble.mjs');
    assert.equal(response.status, 200);
    assert.equal(response.headers['content-type'], 'application/javascript; charset=utf-8');
    assert.match(response.body.toString('utf8'), /quotaBubbleModules/);
  } finally {
    await dispatcher.close();
    await fs.rm(dataDir, { recursive: true, force: true });
  }
});
