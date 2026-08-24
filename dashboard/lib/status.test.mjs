import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  BOOK_STALE_MS,
  REST_UNAVAILABLE_MS,
  WS_MAX_BACKOFF_MS,
  bookVerification,
  connectionDisplayState,
  integrityDisplay,
  isBookFresh,
  restAvailability,
  sequenceIntegrityBadge,
  wsBackoffDelayMs,
} from './status.mjs';

test('book freshness window', () => {
  const now = 1_000_000;
  assert.equal(isBookFresh(now - 100, now), true);
  assert.equal(isBookFresh(now - BOOK_STALE_MS + 1, now), true);
  assert.equal(isBookFresh(now - BOOK_STALE_MS, now), false);
  assert.equal(isBookFresh(null, now), false);
});

test('rest availability is unknown before first success, then decays', () => {
  const now = 5_000_000;
  assert.equal(restAvailability(null, now), null);
  assert.equal(restAvailability(now - 100, now), true);
  assert.equal(restAvailability(now - REST_UNAVAILABLE_MS, now), false);
});

test('connection display: WS truth first, REST only splits reconnecting/unavailable', () => {
  // A successful /book alone must never claim a connected stream.
  assert.equal(connectionDisplayState('connecting', true), 'reconnecting');
  assert.equal(connectionDisplayState('connecting', null), 'connecting');
  assert.equal(connectionDisplayState('connected', false), 'connected');
  assert.equal(connectionDisplayState('reconnecting', true), 'reconnecting');
  assert.equal(connectionDisplayState('reconnecting', false), 'unavailable');
});

test('verified book requires sequence > 0 AND bookReady AND !stale AND fresh depth', () => {
  const healthy = { bookReady: true, stale: false };
  const book = { sequence: 42 };

  assert.equal(bookVerification(book, true, healthy), 'verified');

  assert.equal(bookVerification({ sequence: 0 }, true, healthy), 'stale');
  assert.equal(bookVerification(null, false, healthy), 'stale');
  assert.equal(bookVerification(book, false, healthy), 'stale');
  assert.equal(bookVerification(book, true, { bookReady: true, stale: true }), 'unverified');
  assert.equal(bookVerification(book, true, { bookReady: false, stale: false }), 'unverified');
  // Recent depth without health yet: shown with a warning, never "verified".
  assert.equal(bookVerification(book, true, null), 'awaiting-health');
});

test('sequence integrity badge never derives from checksum failures', () => {
  assert.deepEqual(sequenceIntegrityBadge(null), null);
  assert.equal(
    sequenceIntegrityBadge({ bookReady: true, stale: false }).label,
    'Sequence integrity ✓',
  );
  assert.equal(sequenceIntegrityBadge({ bookReady: true, stale: false }).ok, true);
  assert.equal(sequenceIntegrityBadge({ bookReady: false, stale: false }).label, 'integrity warning');
});

test('OKX integrity wording never claims checksum verification', () => {
  const okx = integrityDisplay({
    exchange: 'okx',
    integrity: 'seqId/prevSeqId',
    checksumFailures: 0,
  });
  assert.equal(okx.mode, 'seqId/prevSeqId');
  assert.match(okx.checksum, /N\/A — deprecated by OKX/);
  assert.doesNotMatch(okx.checksum, /verified/i);

  // Missing integrity field still defaults to the OKX-safe wording.
  const fallback = integrityDisplay({ exchange: 'okx', checksumFailures: 0 });
  assert.equal(fallback.mode, 'seqId/prevSeqId');
  assert.match(fallback.checksum, /deprecated by OKX/);

  // Generic checksum-enabled feed may report real verification status.
  const generic = integrityDisplay({ integrity: 'sequence+checksum', checksumFailures: 2 });
  assert.equal(generic.mode, 'sequence+checksum');
  assert.match(generic.checksum, /2 failure\(s\)/);
  const genericClean = integrityDisplay({ integrity: 'sequence+checksum', checksumFailures: 0 });
  assert.equal(genericClean.checksum, 'verified');
});

test('websocket backoff grows exponentially and caps at 15s', () => {
  assert.equal(wsBackoffDelayMs(0), 1000);
  assert.equal(wsBackoffDelayMs(1), 2000);
  assert.equal(wsBackoffDelayMs(2), 4000);
  assert.equal(wsBackoffDelayMs(3), 8000);
  assert.equal(wsBackoffDelayMs(4), WS_MAX_BACKOFF_MS);
  assert.equal(wsBackoffDelayMs(10), WS_MAX_BACKOFF_MS);
});
