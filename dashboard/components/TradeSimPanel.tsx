'use client';

import { useState } from 'react';

import { formatBps, formatUsd } from '@/lib/format.mjs';
import { simulateOrder } from '@/lib/engine';
import type { ExecutionResult, Side, SizeMode } from '@/lib/types';

interface Props {
  result: ExecutionResult | null;
  onResult: (result: ExecutionResult) => void;
}

export default function TradeSimPanel({ result, onResult }: Props) {
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
      setError(String(cause));
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

        <button onClick={() => void submit()} disabled={busy}>
          {busy ? 'running…' : 'run simulation'}
        </button>
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
