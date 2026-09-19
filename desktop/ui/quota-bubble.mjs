function resetCountdown(resetsAt, now) {
  if (resetsAt === null || resetsAt === undefined || resetsAt === '') return '重置时间未知';
  const reset = Number(resetsAt);
  if (!Number.isFinite(reset)) return '重置时间未知';
  const resetMs = reset < 1e12 ? reset * 1000 : reset;
  const remaining = resetMs - now;
  if (remaining <= 0) return '即将重置';
  const minutes = Math.ceil(remaining / 60000);
  const days = Math.floor(minutes / 1440);
  const hours = Math.floor((minutes % 1440) / 60);
  const rest = minutes % 60;
  if (days) return days + '天' + (hours ? hours + '小时' : '') + '后重置';
  if (hours) return hours + '小时' + (rest ? rest + '分钟' : '') + '后重置';
  return minutes + '分钟后重置';
}

function quotaLine(label, windowData, now, stale) {
  const suffix = stale ? '（上次数据）' : '';
  if (!windowData || !Number.isFinite(Number(windowData.usedPercent))) return label + '：暂无数据' + suffix;
  return label + '：已用 ' + Math.round(Number(windowData.usedPercent)) + '% · ' + resetCountdown(windowData.resetsAt, now) + suffix;
}

export function quotaBubbleModules(snapshot, { now = Date.now() } = {}) {
  const windows = snapshot?.windows || {};
  const stale = Boolean(snapshot?.stale);
  return [
    { type: 'text', text: quotaLine('5 小时', windows.fiveHour, now, stale), size: 6, bold: true },
    { type: 'text', text: quotaLine('本周', windows.weekly, now, stale), size: 6, bold: true },
  ];
}

if (typeof window !== 'undefined') window.WhaleQuotaBubble = { quotaBubbleModules };
