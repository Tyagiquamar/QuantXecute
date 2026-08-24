// Pure status/freshness rules for the engine console. Kept framework-free so
// `node --test` can exercise them directly (see status.test.mjs).

// How long a REST /book response is considered current depth.
export const BOOK_STALE_MS = 3000;
// Without a successful REST call inside this window the engine API counts as
// unavailable.
export const REST_UNAVAILABLE_MS = 4000;
// WebSocket reconnection backoff cap.
export const WS_MAX_BACKOFF_MS = 15000;

/**
 * Capped exponential backoff for the /events stream. Attempt 0 is the first
 * retry after a close.
 *
 * @param {number} attempt
 * @returns {number}
 */
export function wsBackoffDelayMs(attempt) {
  return Math.min(1000 * 2 ** attempt, WS_MAX_BACKOFF_MS);
}

/**
 * @param {number | null} bookUpdatedAtMs local clock of last successful /book
 * @param {number} nowMs
 * @returns {boolean}
 */
export function isBookFresh(bookUpdatedAtMs, nowMs) {
  return typeof bookUpdatedAtMs === 'number' && nowMs - bookUpdatedAtMs < BOOK_STALE_MS;
}

/**
 * REST reachability from success timestamps: true when a call succeeded
 * recently, false once it has been failing too long, null while nothing is
 * known yet (before the first attempt resolves).
 *
 * @param {number | null} lastRestSuccessAtMs most recent of /book and /health
 * @param {number} nowMs
 * @returns {boolean | null}
 */
export function restAvailability(lastRestSuccessAtMs, nowMs) {
  if (typeof lastRestSuccessAtMs !== 'number') {
    return null;
  }
  return nowMs - lastRestSuccessAtMs < REST_UNAVAILABLE_MS;
}

/**
 * Honest header state: WebSocket truth first; REST availability only
 * distinguishes reconnecting from unavailable. A successful /book alone never
 * claims a connected stream.
 *
 * @param {'connecting' | 'connected' | 'reconnecting' | 'unavailable'} wsState
 * @param {boolean | null} restAvailable see restAvailability()
 * @returns {'connecting' | 'connected' | 'reconnecting' | 'unavailable'}
 */
export function connectionDisplayState(wsState, restAvailable) {
  if (wsState === 'connected') {
    return 'connected';
  }
  if (restAvailable === null) {
    return 'connecting';
  }
  return restAvailable ? 'reconnecting' : 'unavailable';
}

/**
 * Strict usability for the depth ladder and simulation gating.
 *
 * @param {{ sequence?: number } | null} book latest /book payload
 * @param {boolean} bookFresh result of isBookFresh()
 * @param {{ bookReady?: boolean; stale?: boolean } | null} health
 * @returns {'verified' | 'awaiting-health' | 'unverified' | 'stale'}
 */
export function bookVerification(book, bookFresh, health) {
  if (!book || (book.sequence ?? 0) <= 0 || !bookFresh) {
    return 'stale';
  }
  if (!health) {
    // Recent depth exists but nothing verified it yet.
    return 'awaiting-health';
  }
  if (health.bookReady === true && health.stale === false) {
    return 'verified';
  }
  return 'unverified';
}

/**
 * Sequence-integrity badge. Never derived from checksumFailures: zero
 * failures does not mean checksum verification is active.
 *
 * @param {{ bookReady?: boolean; stale?: boolean } | null} health
 * @returns {{ ok: boolean, label: string } | null}
 */
export function sequenceIntegrityBadge(health) {
  if (!health) {
    return null;
  }
  const anchored = health.bookReady === true && health.stale === false;
  return {
    ok: anchored,
    label: anchored ? 'Sequence integrity ✓' : 'integrity warning',
  };
}

/**
 * Integrity wording that matches what the engine actually verifies. The OKX
 * books checksum was deprecated by the exchange (fixed to 0), so an OKX /
 * sequence-only feed must never display "checksum ✓".
 *
 * @param {{ exchange?: string; integrity?: string; checksumFailures?: number } | null} health
 * @returns {{ mode: string, checksum: string }}
 */
export function integrityDisplay(health) {
  const mode = health?.integrity ?? '';
  if (mode.includes('checksum')) {
    // Generic checksum-enabled feed: real verification is possible.
    const failures = health?.checksumFailures ?? 0;
    return {
      mode,
      checksum: failures > 0 ? `${failures} failure(s)` : 'verified',
    };
  }
  // seqId/prevSeqId mode (current OKX): checksum plays no part.
  return {
    mode: mode || 'seqId/prevSeqId',
    checksum: 'N/A — deprecated by OKX',
  };
}
