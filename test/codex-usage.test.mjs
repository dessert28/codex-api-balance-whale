import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { readUsageSnapshot } from '../runtime/codex-usage.mjs';

async function fixture(lines) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'api-balance-whale-'));
  const sessions = path.join(root, 'sessions', '2026', '09', '18');
  await fs.mkdir(sessions, { recursive: true });
  const file = path.join(sessions, 'rollout-test.jsonl');
  await fs.writeFile(file, lines.map(line => JSON.stringify(line)).join('\n') + '\n', 'utf8');
  return { root, file };
}

const meta = {
  type: 'session_meta',
  timestamp: '2026-09-18T01:00:00.000Z',
  payload: { id: 'session-1', model: 'gpt-5.6-terra' },
};

test('aggregates token deltas and latest five-hour/weekly rate limits', async () => {
  const { root } = await fixture([
    meta,
    { type: 'event_msg', timestamp: '2026-09-18T01:05:00.000Z', payload: {
      type: 'token_count',
      info: {
        total_token_usage: { input_tokens: 100, output_tokens: 20 },
        last_token_usage: { input_tokens: 100, output_tokens: 20 },
      },
      rate_limits: {
        primary: { used_percent: 12, window_minutes: 300, resets_at: 1789707891 },
        secondary: { used_percent: 34, window_minutes: 10080, resets_at: 1789805325 },
      },
    } },
    { type: 'event_msg', timestamp: '2026-09-18T01:06:00.000Z', payload: {
      type: 'token_count',
      info: {
        total_token_usage: { input_tokens: 160, output_tokens: 30 },
        last_token_usage: { input_tokens: 60, output_tokens: 10 },
      },
      rate_limits: {
        primary: { used_percent: 15, window_minutes: 300, resets_at: 1789707891 },
        secondary: { used_percent: 35, window_minutes: 10080, resets_at: 1789805325 },
      },
    } },
  ]);

  const snapshot = await readUsageSnapshot({
    codexHome: root,
    statePath: path.join(root, 'state.json'),
    now: new Date('2026-09-18T01:07:00.000Z'),
  });

  assert.equal(snapshot.todayTokens, 190);
  assert.equal(snapshot.last7dTokens, 190);
  assert.equal(snapshot.currentModel, 'gpt-5.6-terra');
  assert.deepEqual(snapshot.windows, {
    fiveHour: { usedPercent: 15, resetsAt: 1789707891000 },
    weekly: { usedPercent: 35, resetsAt: 1789805325000 },
  });
  assert.equal(snapshot.lastTurn.deltaTokens, 70);
});

test('seeds existing history without emitting a historical turn', async () => {
  const { root } = await fixture([
    meta,
    { type: 'event_msg', timestamp: '2026-09-18T01:05:00.000Z', payload: {
      type: 'token_count',
      info: { total_token_usage: { input_tokens: 100, output_tokens: 25 } },
    } },
  ]);

  const statePath = path.join(root, 'state.json');
  const snapshot = await readUsageSnapshot({ codexHome: root, statePath, now: new Date('2026-09-18T01:06:00.000Z') });

  assert.equal(snapshot.todayTokens, 125);
  assert.equal(snapshot.lastTurn, null);
});

test('ignores malformed lines and reports missing rate limits without failing', async () => {
  const { root } = await fixture([
    meta,
    '{not-json}',
    { type: 'event_msg', timestamp: '2026-09-18T01:05:00.000Z', payload: {
      type: 'token_count',
      info: { total_token_usage: { input_tokens: 7, output_tokens: 3 } },
    } },
  ]);

  const snapshot = await readUsageSnapshot({ codexHome: root, statePath: path.join(root, 'state.json'), now: new Date('2026-09-18T01:06:00.000Z') });

  assert.equal(snapshot.todayTokens, 10);
  assert.equal(snapshot.windows.fiveHour, null);
  assert.equal(snapshot.windows.weekly, null);
  assert.equal(snapshot.stale, true);
  assert.equal(snapshot.error, '暂无可用配额窗口');
});
