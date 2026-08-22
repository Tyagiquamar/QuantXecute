'use client';

import { useEffect, useState } from 'react';

import { fetchBook, subscribeEvents } from '@/lib/engine';
import type { BookState, EngineEvent, ExecutionResult, HealthState } from '@/lib/types';

import DepthLadder from '@/components/DepthLadder';
import HealthPanel from '@/components/HealthPanel';
import LiveHeader from '@/components/LiveHeader';
import TradeSimPanel from '@/components/TradeSimPanel';

export default function Page() {
  const [book, setBook] = useState<BookState | null>(null);
  const [health, setHealth] = useState<HealthState | null>(null);
  const [result, setResult] = useState<ExecutionResult | null>(null);
  const [connected, setConnected] = useState(false);

  useEffect(() => {
    const close = subscribeEvents((event: EngineEvent) => {
      setConnected(true);
      setHealth(event.health);
      setBook((previous) =>
        previous
          ? { ...previous, mid: event.mid, spreadBps: event.spreadBps, sequence: event.sequence }
          : previous,
      );
    });

    return () => {
      close();
    };
  }, []);

  useEffect(() => {
    if (!connected) {
      fetchBook()
        .then((data) => {
          setConnected(true);
          setBook(data);
        })
        .catch(() => setConnected(false));
    }
  }, [connected]);

  return (
    <main>
      <LiveHeader book={book} health={health} connected={connected} />
      <div className="grid">
        <DepthLadder book={book} />
        <TradeSimPanel onResult={setResult} result={result} />
        <HealthPanel health={health} />
      </div>
    </main>
  );
}
