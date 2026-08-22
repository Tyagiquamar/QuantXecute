# QuantXExecute

![CI](https://github.com/Tyagiquamar/QuantXecute/actions/workflows/ci.yml/badge.svg)

Real-time market-data and **execution-simulation engine** for L2 order books, written in C++20.

QuantXExecute reconstructs an exchange order book incrementally from snapshot + delta streams, simulates market-order execution against live depth with sound units (VWAP, basis points and USD), and proves a hard engineering guarantee:

> **Parity invariant:** given a recorded sequence of market events, deterministic replay produces a byte-identical order book and identical execution results to live processing.

The invariant is enforced by `qx.parity_test`, a required member of `make verify`.

## What it is / what it is not

| It is | It is not |
|---|---|
| A correctness-first L2 book reconstruction engine | An HFT product — no kernel bypass, colocation or NIC tuning claims |
| An execution-cost simulator with reviewer-proof accounting (bps + USD) | A quant-research tool — no unvalidated statistical/ML models on the product surface |
| Deterministically recordable and replayable | A matching engine — no order lifecycle, no order placement, no strategy backtesting |
| Simulated execution against *observed* L2 depth | Connected to any trading account |

## OKX integrity model (current production behavior)

- Real OKX v5 `books` sequencing uses **`seqId` / `prevSeqId`**, not dense `+1` counters.
  - Snapshot: `prevSeqId = -1`, establishes the baseline.
  - Update: accepted iff `prevSeqId` equals the previously accepted `seqId`. `seqId` itself may jump forward arbitrarily, repeat (`asks: []`, `bids: []` keepalive) or move **lower** (maintenance reset). None of those are false gaps.
  - Mismatched `prevSeqId`: the book is invalidated, a gap is counted and a fresh snapshot is requested. The bad update is never applied.
- OKX **deprecated the books checksum on 2026-06-23**: the field still arrives but is fixed to `0`. QuantXExecute therefore runs OKX under an explicit `IntegrityPolicy::SequenceOnly` policy and never treats `checksum: 0` as verification data or advertises CRC32 as a live guarantee.
- Generic checksum-capable feeds can enable `IntegrityPolicy::SequenceAndChecksum`; there a genuine mismatch invalidates the book (`bookReady=false`) until a fresh snapshot recovers it.
- Feeds without usable transport sequencing run `Mode::Arrival`, which stamps a local monotonic counter — a deliberately documented weaker guarantee.

## Repository layout

```
core/     quantxecute_core: book (pure applier), strict decoder, sequence
          validator (owns feed continuity), execution simulator, recorder,
          replay, metrics. No UI, no network.
feed/     quantxecute_feed: resilient feed client (reconnect/backoff/
          staleness/gap-resync) over a transport-agnostic FeedSource;
          OkxWebSocketSource (production TLS WebSocket transport).
server/   quantxecute-server: REST + WebSocket API; replay & live engine
          modes; quantxecute-live-smoke manual verification tool.
console/  Optional Windows/ImGui desktop console (QX_BUILD_CONSOLE=OFF by
          default, enabled explicitly for native development).
dashboard/ Next.js engineering console (depth ladder, trade simulation,
          book health incl. parity status) deployable to Vercel.
docs/     architecture.md, benchmarks.md (measured numbers only).
third_party/ vendored single-header doctest + nlohmann/json.
```

## Build & verify

Requirements: Docker (the verification toolchain runs in a Linux container; no local compiler needed) or any Linux box with gcc/cmake/ninja.

```bash
make docker-image   # one-time: gcc:bookworm + cmake + ninja + clang-tidy
make verify         # full gate:
                    #   1. ASan+UBSan build (production libs instrumented), full ctest
                    #   2. ThreadSanitizer pass (production libs instrumented)
                    #   3. clang-tidy over core/src, feed/src, server/src
make bench          # Release-mode benchmarks, prints p50/p95/p99 percentiles
cd dashboard && npm install && npm test && npx tsc --noEmit
```

GitHub Actions enforces all of it on every PR and on `main`: ASan+UBSan, TSan, clang-tidy, dashboard tests/typecheck/build, plus a Docker build-and-replay smoke test.

## Engine modes

**Replay** — deterministic fixture playback (explicitly selected, never a silent fallback):

```bash
quantxecute-server --mode replay --log core/fixtures/btc_snapshot_deltas.jsonl --port 8080
```

**Live** — real public OKX market data:

```bash
quantxecute-server --mode live --exchange okx --instrument BTC-USDT --channel books
```

Environment variables (container-friendly): `QX_MODE`, `QX_LOG`, `QX_OKX_WS_URL`,
`QX_INSTRUMENT`, `QX_CHANNEL`, `QX_EXCHANGE`, `QX_ALLOWED_ORIGINS`, `PORT`.

If live mode cannot connect, `/health` reports `connected: false` /
`bookReady: false`. The server never silently switches between fixture
replay and live data.

### Data flow

```
LIVE:  OKX WS -> OkxWebSocketSource -> Decoder -> SequenceValidator
            -> Book -> ExecutionSimulator -> REST/WS API -> dashboard

REPLAY: JSONL -> EventLogReader -> same validator/application path
            -> Book -> same ExecutionSimulator -> same API/dashboard
```

## Server API

| Endpoint | Method | Description |
|---|---|---|
| `/health` | GET | mode/exchange/instrument/channel, connected, bookReady, stale, sequenceGaps, reconnects, malformedMessages, lastSeqId, lastMessageAgeMs, integrity mode |
| `/book` | GET | current book JSON (`bids[]`, `asks[]`, `mid`, `spreadBps`, `sequence`) |
| `/simulate` | POST | `{side:"buy"\|"sell", mode:"notional"\|"base", size:number, feeBps:number}` → full execution result; 503 when the book is not ready |
| `/events` | WS | 1 Hz push of mid/spread/sequence/mode/health |

CORS is a narrow allowlist: configure `QX_ALLOWED_ORIGINS=<vercel-origin>` in
production (comma-separated); localhost origins are always allowed for dev.

## Local demo (docker compose)

```bash
docker compose up --build
# engine replays core/fixtures/btc_snapshot_deltas.jsonl continuously
curl localhost:8080/book        # non-empty ladder, sequence > 0
curl -X POST localhost:8080/simulate \
  -d '{"side":"buy","mode":"notional","size":25000,"feeBps":5}'
```

Compose explicitly starts in replay mode — the served book is never empty.

## Dashboard

The Next.js console deploys standalone (e.g. Vercel) against a hosted C++ engine:

```
NEXT_PUBLIC_ENGINE_HTTP_URL=https://<engine-host>
NEXT_PUBLIC_ENGINE_WS_URL=wss://<engine-host>
```

It shows LIVE vs REPLAY identity, exchange/instrument, book freshness,
seq/gap/reconnect counters and integrity mode; simulations hit the hosted
engine's current book via `POST /simulate`. When the engine is unreachable
the dashboard says so instead of showing fake data.

## Hosted architecture

```
             OKX public WebSocket
                      |
                      v
         +---------------------------+
         | C++ QuantXecute Engine    |  long-running Docker service
         | FeedClient + OKX source   |
         | Book + ExecutionSimulator |
         | REST API + WS /events     |
         +-------------+-------------+
                 HTTPS + WSS
                       v
         +---------------------------+
         | Next.js Dashboard         |  Vercel
         +---------------------------+

  Optional native client: Windows/ImGui console (unchanged, opt-in build).
```

## Manual live smoke (optional, never part of CI)

CI stays hermetic: deterministic tests use realistic captured fixtures and
`MockFeedSource`. For a manual check against the real endpoint:

```bash
./build/server/quantxecute-live-smoke   # connects, subscribes, verifies
                                        # snapshot + incremental continuity
```

## Benchmarks

Measured numbers live in [docs/benchmarks.md](docs/benchmarks.md), populated from actual runs of `make bench`. Nothing is claimed before it is measured.

## License

See [LICENSE](LICENSE).