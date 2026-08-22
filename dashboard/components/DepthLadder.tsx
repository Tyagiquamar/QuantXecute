'use client';

import { buildLadder, formatPrice } from '@/lib/format.mjs';
import type { BookState } from '@/lib/types';

export default function DepthLadder({ book }: { book: BookState | null }) {
  const rows = book ? buildLadder(book) : [];

  return (
    <section className="panel">
      <h2>Depth Ladder</h2>
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
