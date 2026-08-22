'use client';

import type { HealthState } from '@/lib/types';

export default function HealthPanel({ health }: { health: HealthState | null }) {
  if (!health) {
    return (
      <section className="panel">
        <h2>Book Health</h2>
        <p style={{ color: 'var(--muted)' }}>waiting for engine data…</p>
      </section>
    );
  }

  const parityOk = !health.stale && health.checksumFailures === 0;

  return (
    <section className="panel">
      <h2>Book Health</h2>

      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', marginBottom: 10 }}>
        <span className={`badge ${health.bookReady ? 'ok' : 'warn'}`}>
          {health.bookReady ? 'book ready ✓' : 'no book'}
        </span>
        <span className={`badge ${parityOk ? 'ok' : 'warn'}`}>
          {parityOk ? 'sequence ✓ / checksum ✓' : 'integrity warning'}
        </span>
      </div>

      <Stat label="connected" value={String(health.connected)} />
      <Stat label="messages accepted" value={String(health.messagesAccepted)} />
      <Stat label="reconnects" value={String(health.reconnects)} />
      <Stat label="sequence gaps" value={String(health.sequenceGaps)} />
      <Stat label="stale-rejected deltas" value={String(health.staleRejected)} />
      <Stat label="malformed messages" value={String(health.malformedMessages)} />
      <Stat label="checksum failures" value={String(health.checksumFailures)} />
      <Stat label="last message age" value={`${health.lastMessageAgeMs} ms`} />
    </section>
  );
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div className="stat">
      <span>{label}</span>
      <span className="value">{value}</span>
    </div>
  );
}
