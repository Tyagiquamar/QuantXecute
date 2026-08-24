'use client';

import { useState } from 'react';

import { formatBps, formatUsd } from '@/lib/format.mjs';
import { simulateOrder } from '@/lib/engine';
import type { ExecutionResult, Side, SizeMode } from '@/lib/types';

interface Props {
  result: ExecutionResult | null;
  onResult: (result: ExecutionResult) => void;
  /** True only when a verified, fresh, engine-confirmed book exists. */
  canSimulate: boolean;
}

export default function TradeSimPanel({ result, onResult, canSimulate }: Props) {
  const [side, setSide] = useState<Side>('buy');
  const [mode, setMode] = useState<SizeMode>('notional');
  const [size, setSize] = useState('25000');
  const [feeBps, setFeeBps] = useState('5');
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const submit = async () => {
    setBusy(true);
    setError(null);
    try {
      onResult(await simulateOrder({ side, mode, size: Number(size), feeBps: Number(feeBps) }));
    } catch (cause) {
      // The engine refuses simulation with 503 while its book is not ready;
      // surface that honestly instead of inventing a result.
      setError(
        cause instanceof Error && cause.message.includes('503')
          ? 'engine refused simulation: book not ready (503)'
          : String(cause),
      );
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="panel">
      <h2>Trade Simulation</h2>

      <div style={{ display: 'grid', gap: 8 }}>
        <label>
          side
          <select value={side} onChange={(event) => setSide(event.target.value as Side)}>
            <option value="buy">buy</option>
            <option value="sell">sell</option>
          </select>
        </label>

        <label>
          sizing
          <select value={mode} onChange={(event) => setMode(event.target.value as SizeMode)}>
            <option value="notional">notional (USD)</option>
            <option value="base">base quantity</option>
          </select>
        </label>

        <label>
          size
          <input value={size} onChange={(event) => setSize(event.target.value)} inputMode="decimal" />
        </label>

        <label>
          taker fee (bps)
          <input
            value={feeBps}
            onChange={(event) => setFeeBps(event.target.value)}
            inputMode="decimal"
          />
        </label>

        <button
          onClick={() => void submit()}
          disabled={busy || !canSimulate}
          title={canSimulate ? undefined : 'waiting for a verified, fresh engine book'}
        >
          {busy ? 'running…' : 'run simulation'}
        </button>
        {!canSimulate && !busy ? (
          <p style={{ color: 'var(--muted)', margin: 0 }}>
            simulation needs a verified book — waiting for engine data
          </p>
        ) : null}
      </div>

      {error && <p style={{ color: 'var(--ask)' }}>{error}</p>}

      {result && (
        <div style={{ marginTop: 12 }}>
          <Stat label="VWAP" value={result.vwap.toFixed(2)} />
          <Stat label="levels consumed" value={String(result.levelsConsumed)} />
          <Stat label="filled notional" value={formatUsd(result.filledNotional)} />
          <Stat label="slippage" value={formatBps(result.slippageBps)} />
          <Stat label="slippage USD" value={formatUsd(result.slippageUsd)} />
          <Stat label="fee" value={`${formatUsd(result.feeUsd)} (${formatBps(result.feeBps)})`} />
          <Stat label="total cost" value={formatBps(result.totalCostBps)} />
          {result.insufficientLiquidity && (
            <p style={{ color: 'var(--warn)' }}>⚠ insufficient liquidity — partial fill</p>
          )}
        </div>
      )}
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
