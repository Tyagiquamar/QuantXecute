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

// Subscribes to /events with automatic reconnection and exponential backoff.
// Returns a disposer. The browser blocks mixed content automatically, but we
// also refuse to build an insecure ws:// stream when the page itself is
// served over HTTPS (production on Vercel).
export function subscribeEvents(onEvent: (event: EngineEvent) => void): () => void {
  let url = `${wsBase().replace(/\/$/, '')}/events`;
  if (typeof window !== 'undefined' && window.location.protocol === 'https:'
      && url.startsWith('ws://')) {
    url = url.replace(/^ws:/, 'wss:');
  }

  let socket: WebSocket | null = null;
  let retryTimer: ReturnType<typeof setTimeout> | null = null;
  let attempt = 0;
  let disposed = false;

  const connect = () => {
    if (disposed) {
      return;
    }
    socket = new WebSocket(url);
    socket.onopen = () => {
      attempt = 0;
    };
    socket.onmessage = (message) => {
      try {
        onEvent(JSON.parse(message.data as string) as EngineEvent);
      } catch {
        return;
      }
    };
    socket.onclose = () => {
      if (disposed) {
        return;
      }
      const delay = Math.min(1000 * 2 ** attempt, 15000);
      attempt += 1;
      retryTimer = setTimeout(connect, delay);
    };
    socket.onerror = () => socket?.close();
  };

  connect();

  return () => {
    disposed = true;
    if (retryTimer !== null) {
      clearTimeout(retryTimer);
    }
    socket?.close();
  };
}