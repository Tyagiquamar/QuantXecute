// Framework-free /events connection lifecycle so the reconnect policy is
// unit-testable in plain node (no DOM, no globals mutated). The browser
// wrapper lives in engine.ts.
import { wsBackoffDelayMs } from './status.mjs';

/**
 * @typedef {Object} EventSocketHandlers
 * @property {(event: any) => void} [onEvent] parsed JSON payload
 * @property {() => void} [onOpen]
 * @property {() => void} [onClose]
 * @property {(error?: unknown) => void} [onError]
 * @property {(attempt: number, delayMs: number) => void} [onReconnecting]
 */

/**
 * Opens an events socket and owns its reconnection loop.
 *
 * @param {Object} options
 * @param {string} options.url absolute /events URL
 * @param {(url: string) => unknown} options.webSocketFactory constructor-like
 * @param {EventSocketHandlers} options.handlers invoked only while active
 * @param {(fn: () => void, delayMs: number) => unknown} [options.schedule]
 * @param {(handle: unknown) => void} [options.cancel]
 * @returns {() => void} disposer; after it runs no handler fires again and no
 *   further reconnect can be scheduled
 */
export function connectEventSocket(options) {
  const { url, webSocketFactory, handlers } = options;
  const schedule = options.schedule ?? ((fn, delayMs) => setTimeout(fn, delayMs));
  const cancel = options.cancel ?? ((handle) => clearTimeout(handle));

  let disposed = false;
  let socket = null;
  let retryHandle = null;
  let attempt = 0;

  const open = () => {
    if (disposed) {
      return;
    }
    socket = webSocketFactory(url);

    socket.onopen = () => {
      if (disposed) {
        return;
      }
      attempt = 0;
      handlers.onOpen?.();
    };

    socket.onmessage = (message) => {
      if (disposed) {
        return;
      }
      try {
        handlers.onEvent?.(JSON.parse(message.data));
      } catch {
        return;
      }
    };

    socket.onclose = () => {
      if (disposed) {
        return;
      }
      handlers.onClose?.();
      const delay = wsBackoffDelayMs(attempt);
      attempt += 1;
      handlers.onReconnecting?.(attempt, delay);
      retryHandle = schedule(open, delay);
    };

    socket.onerror = (error) => {
      if (disposed) {
        return;
      }
      handlers.onError?.(error);
      // Surface the failure as a close so onclose owns reconnect scheduling.
      if (typeof socket.close === 'function') {
        socket.close();
      } else {
        socket.onclose?.({});
      }
    };
  };

  open();

  return () => {
    disposed = true;
    if (retryHandle !== null) {
      cancel(retryHandle);
      retryHandle = null;
    }
    if (socket !== null && typeof socket.close === 'function') {
      const closing = socket;
      socket = null;
      // Real sockets fire their own close event; the disposed guards make
      // that a no-op for handlers.
      closing.onclose = null;
      closing.onerror = null;
      closing.onmessage = null;
      closing.onopen = null;
      closing.close();
    }
  };
}
