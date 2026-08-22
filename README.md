# QuantXExecute

Real-time market-data and **execution-simulation engine** for L2 order books, written in C++20.

QuantXExecute reconstructs an exchange order book incrementally from snapshot + delta streams, simulates market-order execution against live depth with sound units (VWAP, basis points and USD), and proves a hard engineering guarantee:

> **Parity invariant:** given a recorded sequence of market events, deterministic replay produces a byte-identical order book and identical execution results to live processing.

The invariant is enforced by `qx.parity_test`, a required member of `make verify`.

## What it is / what it is not

| It is | It is not |
|---|---|
| A correctness-first L2 book reconstruction engine | An HFT product — no kernel bypass, colocation or NIC tuning claims |
| An execution-cost simulator with reviewer-proof accounting (bps + USD) | A quant-research tool — no unvalidated statistical/ML models on the product surface |
| Deterministically recordable and replayable | A matching engine — no order lifecycle or strategy backtesting |

## Repository layout

```
core/     quantxecute_core: book, decoder, sequence validator, execution
          simulator, recorder, replay, metrics. No UI, no network.
feed/     quantxecute_feed: resilient feed client (reconnect/backoff/
          staleness/gap-resync) over a transport-agnostic FeedSource.
server/   quantxecute-server: REST + WebSocket API over the engine.
console/  Optional legacy ImGui desktop client.
dashboard/ Next.js engineering console (depth ladder, trade simulation,
          book health incl. parity status).
docs/     architecture.md, benchmarks.md (measured numbers only).
third_party/ vendored single-header doctest + nlohmann/json.
```

## Build & verify

Requirements: Docker (the verification toolchain runs in a Linux container; no local compiler needed) or any Linux box with gcc/cmake/ninja.

```bash
make docker-image   # one-time: gcc:bookworm + cmake + ninja + clang-tidy
make verify         # full gate:
                    #   1. ASan+UBSan build, full ctest run
                    #   2. ThreadSanitizer pass
                    #   3. clang-tidy over engine sources
make bench          # Release-mode benchmarks, prints p50/p95/p99 percentiles
cd dashboard && npm ci && npm test   # dashboard formatting unit tests (node:test)
```

## Server API

`quantxecute-server` exposes the running engine state:

| Endpoint | Method | Description |
|---|---|---|
| `/health` | GET | feed health JSON (reconnects, gaps, malformed, staleness, checksum failures) |
| `/book` | GET | current book JSON (`bids[]`, `asks[]`, `mid`, `spreadBps`, `sequence`) |
| `/simulate` | POST | `{side:"buy"\|"sell", mode:"notional"\|"base", size:number, feeBps:number}` → full execution result |
| `/events` | WS | 1 Hz push of mid/spread/sequence/health |

Run against a recorded fixture (offline):

```bash
docker compose up --build
# engine replays core/fixtures/btc_snapshot_deltas.jsonl continuously
curl localhost:8080/book
curl -X POST localhost:8080/simulate \
  -d '{"side":"buy","mode":"notional","size":25000,"feeBps":5}'
```

Point the dashboard at it with `NEXT_PUBLIC_ENGINE_URL=http://localhost:8080` and `npm run dev`.

## Benchmarks

Measured numbers live in [docs/benchmarks.md](docs/benchmarks.md), populated from actual runs of `make bench`. Nothing is claimed before it is measured.

## License

See [LICENSE](LICENSE).
