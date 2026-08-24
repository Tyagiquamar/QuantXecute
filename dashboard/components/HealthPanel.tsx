'use client';

import { integrityDisplay, sequenceIntegrityBadge } from '@/lib/status.mjs';
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

  const sequenceBadge = sequenceIntegrityBadge(health);
  const integrity = integrityDisplay(health);

  return (
    <section className="panel">
      <h2>Book Health</h2>

      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', marginBottom: 10 }}>
        <span className={`badge ${health.bookReady ? 'ok' : 'warn'}`}>
          {health.bookReady ? 'book ready ✓' : 'no book'}
        </span>
        {sequenceBadge ? (
          <span className={`badge ${sequenceBadge.ok ? 'ok' : 'warn'}`}>{sequenceBadge.label}</span>
        ) : null}
      </div>

      <Stat label="connected" value={String(health.connected)} />
      <Stat label="integrity mode" value={integrity.mode} />
      <Stat label="checksum" value={integrity.checksum} />
      {typeof health.lastSeqId === 'number' ? (
        <Stat label="last seqId" value={String(health.lastSeqId)} />
      ) : null}
      <Stat label="messages accepted" value={String(health.messagesAccepted)} />
      <Stat label="reconnects" value={String(health.reconnects)} />
      <Stat label="sequence gaps" value={String(health.sequenceGaps)} />
      <Stat label="stale-rejected deltas" value={String(health.staleRejected)} />
      <Stat label="malformed messages" value={String(health.malformedMessages)} />
      <Stat label="checksum failures (lifetime)" value={String(health.checksumFailures)} />
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
