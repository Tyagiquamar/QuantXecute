'use client';

import { formatBps, formatPrice } from '@/lib/format.mjs';
import type { BookState, HealthState } from '@/lib/types';

interface Props {
  book: BookState | null;
  health: HealthState | null;
  connected: boolean;
}

export default function LiveHeader({ book, health, connected }: Props) {
  const sequenceOk = (book?.sequence ?? 0) > 0 && !health?.stale;

  return (
    <header className="panel" style={{ margin: 16 }}>
      <h2>QuantXExecute — Live Engine</h2>
      <div style={{ display: 'flex', gap: 12, flexWrap: 'wrap' }}>
        <span className={`badge ${connected ? 'ok' : 'warn'}`}>
          {connected ? '● engine connected' : '○ engine offline'}
        </span>
        <span className="badge">mid {formatPrice(book?.mid ?? NaN)}</span>
        <span className="badge">spread {formatBps(book?.spreadBps ?? NaN)}</span>
        <span className="badge">seq {sequenceOk ? `✓ ${book?.sequence}` : '—'}</span>
        <span className="badge">{health ? `${health.messagesAccepted} msgs` : '—'}</span>
      </div>
    </header>
  );
}
