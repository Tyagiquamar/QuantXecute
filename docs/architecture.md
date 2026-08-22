# Architecture

## Component split

```
                 +----------------------------------------------+
                 |  quantxecute_core  (no UI, no network)       |
                 |                                              |
 raw frames ---> |  Decoder -> SequenceValidator -> Book        |
 (FeedSource)    |                                   |          |
                 |                                   v          |
                 |                        ExecutionSimulator    |
                 |                                   |          |
                 |  Recorder <- events   ReplayEngine -> Book   |
                 |                                   |          |
                 |                                   v          |
                 |                                    Metrics   |
                 +------------------+---------------+-----------+
                          |                    |
                    quantxecute_feed      quantxecute-server
                    (reconnect, backoff,  (REST / WebSocket)
                     staleness, resync)        |
                                               |
                                     Next.js dashboard
```

- **core** is UI-free and network-free so every line of correctness logic is unit-testable under sanitizers.
- **feed** wraps any transport behind `FeedSource`; the resilient state machine (`FeedClient`) drives decode -> sequence validation -> book apply -> checksum verify. The real WebSocket transport binds at the server layer; tests inject frames through a mock source with a virtual clock.
- **server** exposes engine state as JSON and pushes `/events` over WebSocket.

## Live vs replay — one core, two drivers

The same `Book` + `ExecutionSimulator` are driven by either:

1. **Live path:** feed frames -> decoder -> validator -> book.
2. **Replay path:** recorded JSONL event log -> `qx::replayInto` -> book.

**Invariant:** `serialize(live_book) == serialize(replay_book)` and identical `ExecutionResult`s for any fixed order set. Enforced by `core/tests/parity_test.cpp`, including a negative control: dropping one delta from the recording must break parity.

## Book update decision flow

Per decoded delta event:

| Condition | Verdict | Action |
|---|---|---|
| `seq == last + 1` | Accept | apply insert/update/delete (size 0 => delete) |
| `seq <= last` | StaleReject | drop message, count it |
| `snapshot` | Accept | reset levels, set sequence baseline |
| `seq > last + 1` | GapResync | clear book, resubscribe for fresh snapshot |

When the feed supplies an OKX-style checksum, the top-25-level CRC32 of the applied book is verified after each accepted event; mismatches are counted and surfaced on `/health`.

## Concurrency posture

Baseline is mutex-guarded state with copy-on-read accessors (`FeedClient::book()` returns an atomic-consistent copy). Source callbacks are never invoked while the client lock is held (re-entrancy safety). Lock-free evolution is deliberately deferred until benchmarks justify it.

## Feed integrity

OKX v5 `books` provides snapshot/update framing plus an optional per-message checksum; when a source lacks per-message sequences (proxy mode), `SequenceValidator::Mode::Arrival` stamps its own monotonic order — a documented weaker guarantee.
