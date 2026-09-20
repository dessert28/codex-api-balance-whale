import test from 'node:test';
import assert from 'node:assert/strict';
import { createCachedAsyncReader } from '../runtime/usage-cache.mjs';

test('coalesces concurrent reads and reuses a snapshot within its TTL', async () => {
  let now = 0;
  let calls = 0;
  const read = createCachedAsyncReader(async () => ({ sequence: ++calls }), {
    ttlMs: 5000,
    now: () => now,
  });

  const [first, concurrent] = await Promise.all([read(), read()]);
  assert.deepEqual(first, { sequence: 1 });
  assert.deepEqual(concurrent, { sequence: 1 });
  assert.equal(calls, 1);

  now = 4999;
  assert.deepEqual(await read(), { sequence: 1 });
  assert.equal(calls, 1);

  now = 5000;
  assert.deepEqual(await read(), { sequence: 2 });
  assert.equal(calls, 2);
});

