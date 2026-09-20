export function createCachedAsyncReader(read, { ttlMs = 5000, now = () => Date.now() } = {}) {
  let cached = null;
  let expiresAt = -Infinity;
  let pending = null;

  return async function cachedRead(...args) {
    const current = now();
    if (cached !== null && current < expiresAt) return cached;
    if (pending) return pending;
    pending = Promise.resolve().then(() => read(...args)).then(value => {
      cached = value;
      expiresAt = now() + Math.max(0, ttlMs);
      return value;
    }).finally(() => { pending = null; });
    return pending;
  };
}

