# Architecture

## Component split

```
                 +----------------------------------------------+
                 |  quantxecute_core  (no UI, no network)       |
                 |                                              |
 raw frames ---> |  Decoder -> SequenceValidator -> Book        |
  (FeedSource)   |   (strict)     (owns feed      (pure         |
                 |                 continuity)     applier)     |
                 |                                   |          |
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
                    (reconnect, backoff,  (REST / WebSocket,
                     staleness, resync;    replay + live modes)
                     OkxWebSocketSource)        |
                                                |
                                      Next.js dashboard (Vercel)
```

- **core** is UI-free and network-free so every line of correctness logic is unit-testable under sanitizers.
- **feed** wraps any transport behind `FeedSource`. The resilient state machine (`FeedClient`) drives decode -> sequence validation -> book apply -> checksum policy. `OkxWebSocketSource` implements the physical TLS session: connect/subscribe/ping-pong/OKX control events. Reconnect policy stays in `FeedClient` — a single source of truth.
- **server** exposes engine state as JSON and pushes `/events` over WebSocket, with explicit replay and live engine modes.

## Live vs replay — one core, two drivers

The same `Book` + `ExecutionSimulator` are driven by either:

1. **Live path:** OKX WS -> `OkxWebSocketSource` -> decoder -> validator -> book -> REST/WS API.
2. **Replay path:** recorded JSONL event log -> `EventLogReader` -> the *same* validator/application path -> book -> identical API.

**Invariant:** `serialize(live_book) == serialize(replay_book)` and identical `ExecutionResult`s for any fixed order set. Enforced by `core/tests/parity_test.cpp` on both the BTC fixture and a real-shape OKX chain (forward jump, keepalive, maintenance reset), including negative controls: dropping one delta or tampering one `prevSeqId` must break parity.

## Sequencing ownership

Continuity semantics live exclusively in `SequenceValidator`; the `Book` is a pure state applier and never judges sequences.

Per decoded OKX delta:

| Condition | Verdict | Action |
|---|---|---|
| snapshot with `prevSeqId == -1` | Accept | reset levels, anchor baseline |
| delta with `prevSeqId == last accepted seqId` | Accept | apply; `seqId` may jump forward, repeat, or move lower |
| repeated `seqId`, empty sides | Accept | keepalive: book unchanged, freshness updated |
| repeated `seqId` carrying levels | StaleReject | drop, count |
| any other `prevSeqId` mismatch | GapResync | invalidate book, count gap, request fresh snapshot |
| delta before any snapshot | GapResync | drop silently (expected churn, not counted as a gap) |

## Integrity policies

| Policy | Behavior |
|---|---|
| `SequenceOnly` | Continuity from seqId/prevSeqId only. Correct for current OKX `books*`: the deprecated checksum field is fixed to 0 since 2026-06-23 and is never verified. |
| `SequenceAndChecksum` | Additionally verifies the supplied CRC32 over the applied top-of-book ladder. A genuine mismatch counts a failure, sets `bookReady=false`, clears the book and requests a fresh snapshot. Simulations refuse to run against an unavailable book. |
| Arrival mode | Feeds without usable sequencing get a local monotonic stamp — a documented weaker guarantee. |

## Numeric safety at the boundary

The decoder rejects anything that does not parse the entire field via
`std::from_chars`: NaN/±Inf, trailing junk (`"100.2abc"`), empty values,
negative price/size, zero price, overflow. OKX millisecond `ts` converts to
nanoseconds with explicit int64 overflow checks. Malformed input never
throws through the feed loop: it increments `malformedMessages` and leaves
the healthy book intact.

## Concurrency posture

Baseline is mutex-guarded state with copy-on-read accessors (`FeedClient::book()` returns an atomic-consistent copy). Source callbacks are never invoked while the client lock is held (re-entrancy safety). Lock-free evolution is deliberately deferred until benchmarks justify it.

## Deployment shape

The C++ engine runs as a long-running Docker service (public HTTPS REST +
WSS endpoints); the Next.js dashboard deploys to Vercel against it using
`NEXT_PUBLIC_ENGINE_HTTP_URL` / `NEXT_PUBLIC_ENGINE_WS_URL`. CORS is a
narrow origin allowlist configured on the engine. The Windows/ImGui console
remains an optional native client built with `QX_BUILD_CONSOLE=ON`.

CI (GitHub Actions) enforces ASan+UBSan, TSan, clang-tidy over production sources, dashboard tests/typecheck/build, and a Docker replay smoke test. Live-network verification is a manual tool (`quantxecute-live-smoke`) and intentionally not part of CI.