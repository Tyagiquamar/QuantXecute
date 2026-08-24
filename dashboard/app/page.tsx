'use client';

import { useEffect, useState } from 'react';

import { fetchBook, fetchHealth, subscribeEvents } from '@/lib/engine';
import {
  bookVerification,
  connectionDisplayState,
  isBookFresh,
  restAvailability,
} from '@/lib/status.mjs';
import type { BookState, EngineEvent, ExecutionResult, HealthState, TransportState } from '@/lib/types';

import DepthLadder from '@/components/DepthLadder';
import HealthPanel from '@/components/HealthPanel';
import LiveHeader from '@/components/LiveHeader';
import TradeSimPanel from '@/components/TradeSimPanel';

// Engineering-console polling rate: fresh depth without hammering the engine.
const POLL_MS = 1000;

function mergeHealth(previous: HealthState | null, next: HealthState): HealthState {
  // Latest arrival wins; spread-merge keeps REST-only fields (integrity)
  // alive across WS-driven updates.
  return { ...(previous ?? {}), ...next };
}

export default function Page() {
  const [wsState, setWsState] = useState<TransportState>('connecting');
  const [book, setBook] = useState<BookState | null>(null);
  const [bookUpdatedAt, setBookUpdatedAt] = useState<number | null>(null);
  const [health, setHealth] = useState<HealthState | null>(null);
  const [healthUpdatedAt, setHealthUpdatedAt] = useState<number | null>(null);
  const [result, setResult] = useState<ExecutionResult | null>(null);
  const [, setClock] = useState(0);

  // WebSocket /events stream: live health/sequence pushes plus honest
  // transport lifecycle. This never touches bids/asks — depth comes from /book.
  useEffect(() => {
    let mounted = true;

    const close = subscribeEvents({
      onEvent(event: EngineEvent) {
        if (!mounted) {
          return;
        }
        setWsState('connected');
        setHealth((previous) => mergeHealth(previous, event.health));
        setBook((previous) =>
          previous
            ? { ...previous, mid: event.mid, spreadBps: event.spreadBps, sequence: event.sequence }
            : previous,
        );
      },
      onOpen() {
        if (mounted) {
          setWsState('connected');
        }
      },
      onClose() {
        if (mounted) {
          setWsState('reconnecting');
        }
      },
      onReconnecting() {
        if (mounted) {
          setWsState('reconnecting');
        }
      },
      onError() {
        // The close handler owns the reconnect transition.
      },
    });

    return () => {
      mounted = false;
      close();
    };
  }, []);

  // Independent REST polling: depth from GET /book, verification state from
  // GET /health. Each has an in-flight guard so a slow response never causes
  // overlapping requests; failures keep the last valid values.
  useEffect(() => {
    let disposed = false;
    let bookInFlight = false;
    let healthInFlight = false;

    const pollBook = async () => {
      if (disposed || bookInFlight) {
        return;
      }
      bookInFlight = true;
      try {
        const next = await fetchBook();
        if (!disposed) {
          setBook(next);
          setBookUpdatedAt(Date.now());
        }
      } catch {
        // Last valid book stays on screen; staleness badges take over.
      } finally {
        bookInFlight = false;
      }
    };

    const pollHealth = async () => {
      if (disposed || healthInFlight) {
        return;
      }
      healthInFlight = true;
      try {
        const next = await fetchHealth();
        if (!disposed) {
          setHealth((previous) => mergeHealth(previous, next));
          setHealthUpdatedAt(Date.now());
        }
      } catch {
        // Same policy: keep the last known health until it expires.
      } finally {
        healthInFlight = false;
      }
    };

    void pollBook();
    void pollHealth();
    const timer = setInterval(() => {
      void pollBook();
      void pollHealth();
      // Drives freshness/unavailability labels between data updates.
      setClock((value) => value + 1);
    }, POLL_MS);

    return () => {
      disposed = true;
      clearInterval(timer);
    };
  }, []);

  const now = Date.now();
  const lastRestSuccessAt =
    bookUpdatedAt !== null && healthUpdatedAt !== null
      ? Math.max(bookUpdatedAt, healthUpdatedAt)
      : (bookUpdatedAt ?? healthUpdatedAt);
  const restAvailable = restAvailability(lastRestSuccessAt, now);
  const connectionState = connectionDisplayState(wsState, restAvailable);
  const bookFresh = isBookFresh(bookUpdatedAt, now);
  const verification = bookVerification(book, bookFresh, health);
  const bookVerified = verification === 'verified';

  return (
    <main>
      <LiveHeader
        book={book}
        health={health}
        connectionState={connectionState}
        restAvailable={restAvailable}
      />
      <div className="grid">
        <DepthLadder
          book={book}
          verification={verification}
          updatedAtMs={bookUpdatedAt}
          nowMs={now}
        />
        <TradeSimPanel
          result={result}
          onResult={setResult}
          canSimulate={bookVerified && restAvailable === true}
        />
        <HealthPanel health={health} />
      </div>
    </main>
  );
}
