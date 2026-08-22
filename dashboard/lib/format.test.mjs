import assert from 'node:assert/strict';
import { test } from 'node:test';

import { buildLadder, formatBps, formatPrice, formatUsd } from './format.mjs';

test('formatUsd renders dollars with two decimals', () => {
  assert.equal(formatUsd(7552.5), '$7,552.50');
  assert.equal(formatUsd(0), '$0.00');
  assert.equal(formatUsd(NaN), '-');
});

test('formatBps renders basis points with two decimals', () => {
  assert.equal(formatBps(12.3456), '12.35 bps');
  assert.equal(formatBps(Infinity), '-');
});

test('formatPrice renders grouped prices', () => {
  assert.equal(formatPrice(63000.5), '63,000.5');
  assert.equal(formatPrice(NaN), '-');
});

test('buildLadder interleaves asks descending above bids', () => {
  const ladder = buildLadder(
    {
      bids: [
        { price: 99, size: 1 },
        { price: 98, size: 2 },
      ],
      asks: [
        { price: 101, size: 3 },
        { price: 102, size: 4 },
      ],
    },
    12,
  );

  assert.deepEqual(
    ladder.map((row) => row.price),
    [102, 101, 99, 98],
  );
  assert.equal(ladder[0].side, 'ask');
  assert.equal(ladder[2].side, 'bid');
});
