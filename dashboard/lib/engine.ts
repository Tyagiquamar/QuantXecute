import type { BookState, EngineEvent, ExecutionResult, HealthState, Side, SizeMode } from './types';

const ENGINE_URL = process.env.NEXT_PUBLIC_ENGINE_URL ?? 'http://localhost:8080';

async function fetchJson<T>(path: string): Promise<T> {
  const response = await fetch(`${ENGINE_URL}${path}`, { cache: 'no-store' });
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
  return fetch(`${ENGINE_URL}/simulate`, {
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

export function subscribeEvents(onEvent: (event: EngineEvent) => void): () => void {
  const wsUrl = ENGINE_URL.replace(/^http/, 'ws') + '/events';
  let socket: WebSocket | null = null;
  let retryTimer: ReturnType<typeof setTimeout> | null = null;

  const connect = () => {
    socket = new WebSocket(wsUrl);
    socket.onmessage = (message) => {
      try {
        onEvent(JSON.parse(message.data as string) as EngineEvent);
      } catch {
        return;
      }
    };
    socket.onclose = () => {
      retryTimer = setTimeout(connect, 2000);
    };
    socket.onerror = () => socket?.close();
  };

  connect();

  return () => {
    if (retryTimer !== null) {
      clearTimeout(retryTimer);
    }
    socket?.close();
  };
}
