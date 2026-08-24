import { connectEventSocket } from './events-socket.mjs';
import type { BookState, EngineEvent, ExecutionResult, HealthState, Side, SizeMode } from './types';

// Hosted deployments configure both URLs explicitly (Vercel env vars).
// NEXT_PUBLIC_ENGINE_URL remains supported as a combined convenience for
// local development against `npm run dev` + docker compose.
const HTTP_BASE =
  process.env.NEXT_PUBLIC_ENGINE_HTTP_URL ??
  process.env.NEXT_PUBLIC_ENGINE_URL ??
  'http://localhost:8080';

function wsBase(): string {
  if (process.env.NEXT_PUBLIC_ENGINE_WS_URL) {
    return process.env.NEXT_PUBLIC_ENGINE_WS_URL;
  }
  // Derive from the HTTP base; https -> wss is enforced below.
  return HTTP_BASE.replace(/^http/, 'ws');
}

async function fetchJson<T>(path: string): Promise<T> {
  const response = await fetch(`${HTTP_BASE}${path}`, { cache: 'no-store' });
  if (!response.ok) {
    throw new Error(`engine ${path} failed: ${response.status}`);
  }
  return (await response.json()) as T;
}

export function fetchBook(): Promise<BookState> {
  return fetchJson<BookState>('/book');
}

export function fetchHealth(): Promise<HealthState> {
  return fetchJson<HealthState>('/health');
}

export interface SimulateInput {
  side: Side;
  mode: SizeMode;
  size: number;
  feeBps: number;
}

export function simulateOrder(input: SimulateInput): Promise<ExecutionResult> {
  return fetch(`${HTTP_BASE}/simulate`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(input),
  }).then(async (response) => {
    const payload = await response.json();
    if (!response.ok) {
      throw new Error(`simulate failed: ${response.status}`);
    }
    return payload as ExecutionResult;
  });
}

// Subscribes to /events with automatic reconnection and capped exponential
// backoff. Returns a disposer. The browser blocks mixed content
// automatically, but we also refuse to build an insecure ws:// stream when
// the page itself is served over HTTPS (production on Vercel).
export interface EventSubscriptionHandlers {
  onEvent(event: EngineEvent): void;
  onOpen?(): void;
  onClose?(): void;
  onError?(error?: unknown): void;
  /** Fired after a close, with the upcoming retry attempt (1-based) and delay. */
  onReconnecting?(attempt: number, delayMs: number): void;
}

export function subscribeEvents(handlers: EventSubscriptionHandlers): () => void {
  let url = `${wsBase().replace(/\/$/, '')}/events`;
  if (typeof window !== 'undefined' && window.location.protocol === 'https:'
      && url.startsWith('ws://')) {
    url = url.replace(/^ws:/, 'wss:');
  }

  // Reconnection policy, disposal guarantees and handler guards live in
  // events-socket.mjs (unit-tested there); this wrapper only supplies the
  // browser WebSocket and the production URL.
  return connectEventSocket({
    url,
    webSocketFactory: (socketUrl) => new WebSocket(socketUrl),
    handlers,
  });
}