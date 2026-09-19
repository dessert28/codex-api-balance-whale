import fs from 'node:fs/promises';
import path from 'node:path';

const TOKEN_FIELDS = ['input_tokens', 'output_tokens', 'reasoning_output_tokens'];

function number(value) {
  return Number.isFinite(Number(value)) ? Math.max(0, Number(value)) : 0;
}

function totalTokens(value) {
  if (Number.isFinite(Number(value?.total_tokens))) return Math.max(0, Number(value.total_tokens));
  return TOKEN_FIELDS.reduce((sum, key) => sum + number(value?.[key]), 0);
}

function timestamp(value) {
  const parsed = typeof value === 'number' ? (value < 1e12 ? value * 1000 : value) : Date.parse(value);
  return Number.isFinite(parsed) ? parsed : NaN;
}

function emptyWindow() {
  return { fiveHour: null, weekly: null };
}

function normalizeWindow(value) {
  if (!value || !Number.isFinite(Number(value.used_percent))) return null;
  const reset = Number(value.resets_at);
  return {
    usedPercent: Number(value.used_percent),
    resetsAt: Number.isFinite(reset) ? (reset < 1e12 ? reset * 1000 : reset) : null,
  };
}

function latestWindow(rateLimits, windows) {
  const primary = normalizeWindow(rateLimits?.primary);
  const secondary = normalizeWindow(rateLimits?.secondary);
  if (primary) windows.fiveHour = primary;
  if (secondary) windows.weekly = secondary;
}

async function jsonlFiles(root) {
  const files = [];
  async function walk(dir) {
    let entries;
    try { entries = await fs.readdir(dir, { withFileTypes: true }); } catch { return; }
    for (const entry of entries) {
      const file = path.join(dir, entry.name);
      if (entry.isDirectory()) await walk(file);
      else if (entry.isFile() && entry.name.endsWith('.jsonl')) files.push(file);
    }
  }
  await walk(root);
  return files.sort();
}

async function scanFile(file) {
  let text;
  try { text = await fs.readFile(file, 'utf8'); } catch { return null; }
  const events = [];
  let model = '';
  let latestRate = null;
  for (const line of text.split(/\r?\n/)) {
    if (!line.trim()) continue;
    let item;
    try { item = JSON.parse(line); } catch { continue; }
    const payload = item?.payload || {};
    if (item.type === 'session_meta') {
      if (typeof payload.model === 'string') model = payload.model;
      continue;
    }
    if (item.type === 'turn_context' && typeof payload.model === 'string') model = payload.model;
    if (item.type !== 'event_msg' || payload.type !== 'token_count' || !payload.info?.total_token_usage) continue;
    const at = timestamp(item.timestamp) || Date.now();
    const cumulative = totalTokens(payload.info.total_token_usage);
    const last = totalTokens(payload.info.last_token_usage);
    events.push({ at, cumulative, last, model, rateLimits: payload.rate_limits || item.rate_limits || null });
    if (events.at(-1).rateLimits) latestRate = { at, value: events.at(-1).rateLimits };
  }
  return { file, events, model, latestRate };
}

function summarizeFile(scan) {
  const daily = new Map();
  let previous = 0;
  let latest = null;
  for (const event of scan.events) {
    const delta = event.cumulative >= previous ? event.cumulative - previous : event.last;
    const day = new Date(event.at).toISOString().slice(0, 10);
    daily.set(day, (daily.get(day) || 0) + delta);
    if (delta > 0) latest = { ...event, deltaTokens: delta };
    previous = event.cumulative;
  }
  return { daily, finalTotal: previous, eventCount: scan.events.length, latest };
}

export async function readUsageSnapshot({ codexHome, statePath, now = new Date() }) {
  const root = path.join(codexHome, 'sessions');
  const archived = path.join(codexHome, 'archived_sessions');
  const files = [...await jsonlFiles(root), ...await jsonlFiles(archived)];
  let state = {};
  try { state = JSON.parse(await fs.readFile(statePath, 'utf8')); } catch {}
  const previousFiles = state.files || {};
  const nextFiles = {};
  const windows = emptyWindow();
  const daily = new Map();
  let currentModel = '';
  let newestModelAt = -Infinity;
  let newestRateAt = -Infinity;
  let newestTurn = null;
  const nowMs = now instanceof Date ? now.getTime() : timestamp(now);

  for (const file of files) {
    const scan = await scanFile(file);
    if (!scan) continue;
    const summary = summarizeFile(scan);
    const prior = previousFiles[file];
    const changed = !prior || prior.finalTotal !== summary.finalTotal || prior.eventCount !== summary.eventCount;
    nextFiles[file] = { finalTotal: summary.finalTotal, eventCount: summary.eventCount };
    for (const [day, value] of summary.daily) daily.set(day, (daily.get(day) || 0) + value);
    const latestModelEvent = scan.events.at(-1);
    if (latestModelEvent?.model && latestModelEvent.at >= newestModelAt) {
      newestModelAt = latestModelEvent.at;
      currentModel = latestModelEvent.model;
    } else if (scan.model && !currentModel) currentModel = scan.model;
    if (scan.latestRate && scan.latestRate.at >= newestRateAt) {
      newestRateAt = scan.latestRate.at;
      latestWindow(scan.latestRate.value, windows);
    }
    if (changed && summary.latest && (!prior || summary.latest.at >= Number(prior.lastAt || 0))) {
      if (prior || summary.eventCount > 1) newestTurn = { ...summary.latest, model: summary.latest.model || scan.model };
    }
    nextFiles[file].lastAt = summary.latest?.at || prior?.lastAt || null;
  }

  const today = new Date(nowMs).toISOString().slice(0, 10);
  const weekStart = nowMs - 7 * 24 * 60 * 60 * 1000;
  const todayTokens = daily.get(today) || 0;
  let last7dTokens = 0;
  for (const [day, value] of daily) {
    const at = Date.parse(`${day}T00:00:00.000Z`);
    if (at >= weekStart && at <= nowMs) last7dTokens += value;
  }

  const hasWindow = Boolean(windows.fiveHour || windows.weekly);
  const snapshot = {
    generatedAt: new Date(nowMs).toISOString(),
    todayTokens,
    last7dTokens,
    currentModel: currentModel || null,
    windows,
    lastTurn: newestTurn ? {
      model: newestTurn.model || null,
      deltaTokens: newestTurn.deltaTokens,
      timestamp: new Date(newestTurn.at).toISOString(),
    } : null,
    stale: files.length === 0 || !hasWindow,
    error: files.length === 0 ? '暂无 Codex 会话日志' : (!hasWindow ? '暂无可用配额窗口' : null),
  };
  await fs.mkdir(path.dirname(statePath), { recursive: true });
  await fs.writeFile(statePath, JSON.stringify({ version: 1, files: nextFiles }, null, 2), 'utf8');
  return snapshot;
}
