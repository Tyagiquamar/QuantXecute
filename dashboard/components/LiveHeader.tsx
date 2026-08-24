'use client';

import { formatBps, formatPrice } from '@/lib/format.mjs';
import type { BookState, HealthState, TransportState } from '@/lib/types';

interface Props {
  book: BookState | null;
  health: HealthState | null;
  connectionState: TransportState;
  restAvailable: boolean | null;
}

const CONNECTION_BADGES: Record<TransportState, { label: string; className: string }> = {
  connected: { label: '● engine connected', className: 'ok' },
  connecting: { label: '◌ connecting', className: 'warn' },
  reconnecting: { label: '◌ reconnecting', className: 'warn' },
  unavailable: { label: '○ engine unavailable', className: 'warn' },
};

export default function LiveHeader({ book, health, connectionState, restAvailable }: Props) {
  const mode = health?.mode ?? 'unknown';
  const isLive = mode === 'live';
  const bookVerified = (book?.sequence ?? 0) > 0 && !health?.stale && health?.bookReady;

  const badge = CONNECTION_BADGES[connectionState];

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
        <span className={`badge ${badge.className}`}>{badge.label}</span>
        {restAvailable === true && connectionState !== 'connected' ? (
          <span className="badge ok">REST API ✓</span>
        ) : null}
      </div>
      <div style={{ display: 'flex', gap: 12, flexWrap: 'wrap' }}>
        <span className="badge">mid {bookVerified ? formatPrice(book?.mid ?? NaN) : '—'}</span>
        <span className="badge">
          spread {bookVerified ? formatBps(book?.spreadBps ?? NaN) : '—'}
        </span>
        <span className={`badge ${bookVerified ? 'ok' : 'warn'}`}>
          seq {bookVerified ? `✓ ${(health?.lastSeqId ?? book?.sequence)}` : '— no verified book'}
        </span>
        <span className="badge">{health ? `${health.messagesAccepted} msgs` : '—'}</span>
        {health && health.lastMessageAgeMs >= 0 ? (
          <span className="badge">freshness {health.lastMessageAgeMs} ms</span>
        ) : null}
      </div>
    </header>
  );
}
