'use client';

import { formatBps, formatPrice } from '@/lib/format.mjs';
import type { BookState, HealthState } from '@/lib/types';

interface Props {
  book: BookState | null;
  health: HealthState | null;
  connected: boolean;
}

export default function LiveHeader({ book, health, connected }: Props) {
  const mode = health?.mode ?? 'unknown';
  const isLive = mode === 'live';
  const bookUsable = (book?.sequence ?? 0) > 0 && !health?.stale && health?.bookReady;

  return (
    <header className="panel" style={{ margin: 16 }}>
      <h2>QuantXExecute — Engine Console</h2>
      <div style={{ display: 'flex', gap: 12, flexWrap: 'wrap', marginBottom: 8 }}>
        <span className={`badge ${isLive ? 'ok' : ''}`}>
          {isLive ? '● LIVE market data' : `○ ${mode.toUpperCase()} replay`}
        </span>
        {health?.exchange ? (
          <span className="badge">
            {health.exchange}
            {health.instrument ? ` · ${health.instrument}` : ''}
            {health.channel ? ` · ${health.channel}` : ''}
          </span>
        ) : null}
        <span className={`badge ${connected ? 'ok' : 'warn'}`}>
          {connected ? '● engine connected' : '○ engine unavailable'}
        </span>
      </div>
      <div style={{ display: 'flex', gap: 12, flexWrap: 'wrap' }}>
        <span className="badge">mid {bookUsable ? formatPrice(book?.mid ?? NaN) : '—'}</span>
        <span className="badge">
          spread {bookUsable ? formatBps(book?.spreadBps ?? NaN) : '—'}
        </span>
        <span className={`badge ${bookUsable ? 'ok' : 'warn'}`}>
          seq {bookUsable ? `✓ ${(health?.lastSeqId ?? book?.sequence)}` : '— no verified book'}
        </span>
        <span className="badge">{health ? `${health.messagesAccepted} msgs` : '—'}</span>
        {health && health.lastMessageAgeMs >= 0 ? (
          <span className="badge">freshness {health.lastMessageAgeMs} ms</span>
        ) : null}
      </div>
    </header>
  );
}