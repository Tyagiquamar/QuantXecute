# Productionization prompt — status ledger

Tracks the "close the remaining correctness/deployment gaps" work program
against the repository as of 2026-08-23 (post `5370668`). Every item below is
verifiable in-tree; evidence points at the owning files/tests.

Standing constraints honored throughout: architecture preserved (no rewrite),
Windows/ImGui console kept, no order placement, no trading bot, no fake HFT
claims, no silent live→fixture fallback.

---

## Phase 0 — inspect before editing

**DONE.** Control flow confirmed: `FeedSource → FeedClient → Decoder →
SequenceValidator → Book → ExecutionSimulator` and `Recorder → JSONL →
EventLogReader/Replay → Book`. No types or ownership guessed.

## 1. Real OKX seqId / prevSeqId semantics — DONE

- `MarketEvent` carries `sequence` + `hasSequence` + signed
  `std::optional<std::int64_t> prevSequence` (`core/include/qx/MarketEvent.h`);
  `-1` lives in a signed optional, never a `uint64`.
- `SequenceValidator` owns feed continuity exclusively
  (`core/src/SequenceValidator.cpp`):
  - snapshot: accepted iff `prevSeqId == -1`; establishes baseline;
  - delta: accepted iff `prevSeqId == lastSeqId`; `seqId` may jump forward,
    repeat, or move **lower** (maintenance reset);
  - repeated `seqId` is only valid in the empty keepalive form; a repeated
    `seqId` carrying levels is `StaleReject` (prevents double-apply);
  - any other mismatch: `GapResync`, book invalidated, gap counted.
- `Book::applyDelta` holds no sequencing assumptions; continuity logic is not
  duplicated. ProxyBooks/Arrival mode preserved (`Mode::Arrival` stamps a local
  monotonic counter, documented weaker).

## 2. Real-shape OKX fixtures & tests A–G — DONE

- `core/tests/sequence_test.cpp`: cases A (snapshot −1), B (jump ⇒ NOT +1),
  C (chained next), D (no-change keepalive: book untouched, freshness updated,
  no gap), E (real gap: reject/invalidate/count/resubscribe), F (maintenance
  reset to lower seqId, then post-reset chain), G (stale prevSeqId reject),
  plus malformed-snapshot-metadata, pre-snapshot deltas, missing metadata,
  arrival mode, reset-clears-state.
- Decoder suite covers real payload shapes incl. numeric-string seqIds
  (`core/tests/decoder_test.cpp`).
- Feed-level pipeline tests for jump-through, keepalive liveness, gap
  recovery, pre-snapshot churn, stale duplicate
  (`feed/tests/feed_test.cpp`).
- Realistic chain fixture: `core/fixtures/okx_parity_chain.jsonl`
  (snapshot −1 → jump → keepalive → normal → maintenance reset → post-reset).

## 3. Recorder / Replay on real sequencing — DONE

- Recorder persists `seqId`, signed `prevSeqId`, and an explicit
  `"sequenced":false` marker for unsequenced logs
  (`core/src/Recorder.cpp`); `EventLogReader` restores all of it (legacy
  dense-`sequence` logs still readable).
- Replay drives the same `SequenceValidator` semantics as live
  (`core/src/Replay.cpp`), so parity stays meaningful:
  `qx.parity_test` runs the BTC fixture **and** the OKX chain fixture with
  negative controls (dropped delta / tampered `prevSeqId` must break parity)
  (`core/tests/parity_test.cpp`).

## 4. Current OKX checksum policy — DONE

- `IntegrityPolicy::{SequenceOnly, SequenceAndChecksum}`
  (`core/include/qx/SequenceValidator.h`). Under `SequenceOnly` the deprecated
  OKX field (fixed to 0 since 2026-06-23) returns `NotPresent` and is never
  verified or advertised. Generic feeds can still enable checksum verification.
- README, `docs/architecture.md` and dashboard health naming updated
  ("integrity mode", checksum-deprecated note).

## 5. Checksum failure invalidates the book (when checksum is real) — DONE

- Mismatch under `SequenceAndChecksum`: counter incremented, `bookReady=false`,
  book cleared, validator pipeline reset, resubscribe requested; `simulate`
  refuses to run against the corrupted book until a fresh snapshot recovers it
  (`feed/src/FeedClient.cpp`).
- Deterministic proof: `feed_test.cpp`
  "genuinely-enabled checksum mismatch invalidates the book and blocks
  simulation". Not applied to current OKX `books` (checksum=0 ignored by
  policy test "current OKX books policy ignores the deprecated checksum=0
  field").

## 6. Hardened numeric parsing — DONE (final polish this pass)

- Decoder boundary uses `std::from_chars` whole-string parsing
  (`core/src/Decoder.cpp`): price finite > 0, size finite ≥ 0 (0 = delete),
  NaN/Inf/negative/empty/trailing-junk/overflow rejected; `ts` ms→ns with an
  explicit overflow guard; seqId ≥ 0 and prevSeqId ∈ [−1, max] range-checked
  from strings or JSON numbers; malformed frames never throw through the loop
  (counted as malformed).
- **Polish applied:** `EventLogReader` level parsing switched from
  locale-dependent `std::stod` to the same strict whole-string `from_chars`
  rule (`core/src/Recorder.cpp`) so recorded/replayed data obeys the same law.

## 7. Production OKX WebSocket FeedSource — DONE

- `feed/include/qx/feed/OkxWebSocketSource.h` +
  `feed/src/OkxWebSocketSource.cpp`, built as `quantxecute_feed_okx` inside the
  server module (networking stays out of `quantxecute_core`; feed lib itself
  stays transport-free). Uses maintained IXWebSocket (TLS/OpenSSL) — no
  hand-rolled protocol.
- Configurable endpoint (default `wss://ws.okx.com:8443/ws/v5/public`),
  channel/instrument subscription shape per spec, TLS CA configurable.
- Application-level liveness: `ping` after ≤20 s silence (<30 s rule), `pong`
  deadline enforced, missed pongs counted → disconnect → FeedClient backoff.
- Service-upgrade notice (code 64008): counted, clean stop, reconnect via the
  single source of truth — `FeedClient` still owns backoff/state/resubscribe.
- Manual hermetic-safe smoke tool: `server/src/live_smoke.cpp`
  (`quantxecute-live-smoke`).

## 8. Live server mode — DONE

- `--mode replay|live` (+ legacy `--log` implies replay),
  `--exchange/--instrument/--channel/--ws-url/--bind/--port`
  (`server/src/main.cpp`).
- Container env: `QX_MODE`, `QX_LOG`, `QX_OKX_WS_URL`, `QX_INSTRUMENT`,
  `QX_CHANNEL`, `QX_EXCHANGE`, `QX_ALLOWED_ORIGINS`, `PORT`,
  `QX_STALENESS_MS`, `QX_CA_CERT_PATH`.
- No silent fallback: unset mode exits with usage error; live mode reports
  `connected:false` / `bookReady:false` on `/health` instead of switching to
  fixtures (covered by server tests incl. live-unavailable 503 path).
- Replay loops its explicitly-selected fixture rather than serving emptiness.

## 9. Windows desktop app preserved — DONE

- `console/` intact; `QX_BUILD_CONSOLE` defaults OFF and builds opt-in
  (root `CMakeLists.txt`). No desktop-only behavior in core/feed/server.

## 10. Hosted architecture — DONE

- Documented topology (long-running Docker engine ⇄ HTTPS/WSS ⇄ Vercel
  dashboard; console as separate native client) in `README.md` +
  `docs/architecture.md`. Engine never forced into serverless functions.

## 11. Container-ready engine — DONE

- Binds `0.0.0.0`, respects `PORT`, SIGINT/SIGTERM graceful shutdown stopping
  HTTP + feed threads with no orphans (`server/src/main.cpp`).
- `server/Dockerfile`: multi-stage gcc→debian-slim runtime, CA certificates +
  libssl3 for outbound wss, non-root user, deterministic startup logs carrying
  mode/exchange/instrument/channel, `HEALTHCHECK` against `/health`.

## 12. Docker compose replay fix — DONE

- `docker-compose.yml` starts explicitly with `--mode replay --log
  /engine/fixtures/btc_snapshot_deltas.jsonl`; README demo claims match
  behavior; CI docker job asserts non-empty `/book` and working `/simulate`
  (regression guard for the "empty engine" bug class).

## 13. Vercel dashboard deployment — DONE

- `NEXT_PUBLIC_ENGINE_HTTP_URL` / `NEXT_PUBLIC_ENGINE_WS_URL` env config
  (localhost only as dev default) — `dashboard/lib/engine.ts`,
  `dashboard/.env.example`.
- https→wss upgrade enforced browser-side; WS auto-reconnect with capped
  exponential backoff; engine-unreachable renders as unavailable state.
- LIVE vs REPLAY identity, exchange/instrument, freshness, seq/gap/reconnect
  counters, integrity-mode display (`dashboard/components/*`).
- CORS narrowed to configured allowlist (`QX_ALLOWED_ORIGINS`), localhost
  always allowed in dev, `Vary: Origin` set — no blanket `*`
  (`server/src/ApiServer.cpp`).

## 14. Sanitizers over production code — DONE

- Central `cmake/Sanitizers.cmake` applies compile+link instrumentation to
  `quantxecute_core`, `quantxecute_feed`, `quantxecute_feed_okx`, all server
  executables and every test target — production libraries are instrumented,
  not just sanitized test executables linking clean libs.
- ASan+UBSan combined; TSan strictly separate (fatal if mixed); MSVC warns and
  skips. Enforced in CI via `make verify-fast` / `make verify-tsan`.

## 15. GitHub Actions CI — DONE

- `.github/workflows/ci.yml`: push/PR to `main` + `workflow_dispatch`;
  docs-only paths-ignore; jobs: `cpp-sanitizers` (ASan+UBSan ctest),
  `cpp-tsan` (seccomp-scoped), `clang-tidy` (warnings-as-errors over
  core/feed/server sources), `dashboard` (test/typecheck/build), `docker`
  (image build, compose validation, replay boot smoke asserting non-empty
  book + simulation).

---

## Verification

- Full suite green locally (MinGW g++ 16.1, Ninja, Debug): 9/9 ctest suites
  pass (types/book/decoder/sequence/execution/replay/parity/metrics/feed).
- Sanitizer/TSan/clang-tidy/docker gates run in CI (Linux containers) via the
  Makefile contract.
