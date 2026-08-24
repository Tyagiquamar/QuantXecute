import assert from 'node:assert/strict';
import { test } from 'node:test';

import { connectEventSocket } from './events-socket.mjs';

// Deterministic scheduler: tests fire pending retries by hand.
function manualScheduler() {
  const pending = [];
  const api = {
    schedule(fn, delayMs) {
      const task = { fn, delayMs, cancelled: false };
      pending.push(task);
      return task;
    },
    cancel(task) {
      task.cancelled = true;
    },
    run() {
      for (const task of pending.splice(0)) {
        if (!task.cancelled) {
          task.fn();
        }
      }
    },
    get pending() {
      return pending;
    },
  };
  return api;
}

// Minimal WebSocket stand-in the tests can drive directly.
class FakeWebSocket {
  constructor(url) {
    this.url = url;
    this.closed = false;
    FakeWebSocket.created.push(this);
  }

  close() {
    this.closed = true;
    // Real sockets emit onclose after close(); mimic that.
    if (this.onclose && !this._emitted) {
      this._emitted = true;
      const handler = this.onclose;
      handler({});
    }
  }

  emitOpen() {
    this.onopen?.({});
  }

  emitMessage(payload) {
    this.onmessage?.({ data: JSON.stringify(payload) });
  }

  emitClose() {
    this._emitted = true;
    this.closed = true;
    this.onclose?.({});
  }
}

FakeWebSocket.created = [];

test('open resets attempts and delivers parsed events', () => {
  FakeWebSocket.created = [];
  const events = [];
  let opens = 0;

  connectEventSocket({
    url: 'wss://engine.example/events',
    webSocketFactory: (url) => new FakeWebSocket(url),
    handlers: {
      onEvent: (event) => events.push(event),
      onOpen: () => {
        opens += 1;
      },
    },
  });

  const socket = FakeWebSocket.created[0];
  assert.equal(socket.url, 'wss://engine.example/events');

  socket.emitOpen();
  assert.equal(opens, 1);

  socket.emitMessage({ sequence: 7 });
  assert.deepEqual(events, [{ sequence: 7 }]);
});

test('close -> onClose + onReconnecting with capped backoff; disposed never reconnects', () => {
  FakeWebSocket.created = [];
  const scheduler = manualScheduler();
  let closes = 0;
  const reconnects = [];
  const events = [];

  const dispose = connectEventSocket({
    url: 'ws://engine.example/events',
    webSocketFactory: (url) => new FakeWebSocket(url),
    handlers: {
      onEvent: (event) => events.push(event),
      onClose: () => {
        closes += 1;
      },
      onReconnecting: (attempt, delayMs) => reconnects.push([attempt, delayMs]),
    },
    schedule: scheduler.schedule,
    cancel: scheduler.cancel,
  });

  // First drop -> retry in 1s; second drop -> 2s.
  FakeWebSocket.created.at(-1).emitClose();
  assert.equal(closes, 1);
  assert.deepEqual(reconnects[0], [1, 1000]);

  scheduler.run();
  assert.equal(FakeWebSocket.created.length, 2);

  FakeWebSocket.created.at(-1).emitClose();
  assert.deepEqual(reconnects[1], [2, 2000]);

  // Dispose while a retry is pending: timer cancelled, no new socket ever,
  // no further handler invocations (stale callbacks suppressed).
  dispose();
  const socketsAtDispose = FakeWebSocket.created.length;
  scheduler.run();
  assert.equal(FakeWebSocket.created.length, socketsAtDispose);
  assert.equal(reconnects.length, 2);

  FakeWebSocket.created.at(-1).emitClose();
  FakeWebSocket.created.at(-1).emitMessage({ sequence: 999 });
  assert.equal(closes, 2);
  assert.equal(events.filter((event) => event.sequence === 999).length, 0);

  // Backoff cap holds across every scheduled retry.
  assert.ok(reconnects.every(([, delayMs]) => delayMs <= 15000));
});

test('error surfaces through onError and routes into the close path', () => {
  FakeWebSocket.created = [];
  const seen = { errors: 0, closes: 0 };
  const scheduler = manualScheduler();

  const dispose = connectEventSocket({
    url: 'ws://engine.example/events',
    webSocketFactory: (url) => new FakeWebSocket(url),
    handlers: {
      onError: () => {
        seen.errors += 1;
      },
      onClose: () => {
        seen.closes += 1;
      },
    },
    schedule: scheduler.schedule,
    cancel: scheduler.cancel,
  });

  const socket = FakeWebSocket.created[0];
  socket.onerror?.({ message: 'boom' });

  assert.equal(seen.errors, 1);
  // The close path owns reconnection scheduling.
  assert.equal(seen.closes, 1);
  assert.equal(scheduler.pending.length > 0 || FakeWebSocket.created.length === 2, true);

  dispose();
});
