'use client';

import { buildLadder, formatPrice } from '@/lib/format.mjs';
import { BOOK_STALE_MS } from '@/lib/status.mjs';
import type { BookState } from '@/lib/types';

export type BookVerification = 'verified' | 'awaiting-health' | 'unverified' | 'stale';

interface Props {
  book: BookState | null;
  verification: BookVerification;
  updatedAtMs: number | null;
  nowMs: number;
}

const VERIFICATION_NOTICES: Record<BookVerification, string> = {
  verified: '',
  'awaiting-health': 'awaiting health verification',
  unverified: 'engine reports book unavailable',
  stale: 'depth stale — waiting for engine data',
};

export default function DepthLadder({ book, verification, updatedAtMs, nowMs }: Props) {
  const rows = book ? buildLadder(book) : [];
  const fresh = verification !== 'stale';
  const ageMs = updatedAtMs === null ? null : Math.max(0, nowMs - updatedAtMs);
  const notice = VERIFICATION_NOTICES[verification];

  return (
    <section className="panel">
      <h2>Depth Ladder</h2>
      <p style={{ color: 'var(--muted)', margin: '0 0 8px' }}>
        {fresh && ageMs !== null ? `book updated ${ageMs} ms ago` : `depth stale (>${BOOK_STALE_MS / 1000}s)`}
        {notice ? ` · ${notice}` : ''}
      </p>
      {rows.length === 0 && <p style={{ color: 'var(--muted)' }}>waiting for engine data…</p>}
      {rows.map((row) => (
        <div key={`${row.side}-${row.price}`} className={`ladder-row ${row.side}`}>
          <span>{formatPrice(row.price)}</span>
          <span>{row.size.toFixed(4)}</span>
        </div>
      ))}
    </section>
  );
}
