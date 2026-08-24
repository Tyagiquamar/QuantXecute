# Production deployment guide

Target topology (see `docs/architecture.md`): one **long-running Docker host**
runs the C++ engine; the Next.js dashboard runs on **Vercel** and talks to it
over HTTPS + WSS. The engine cannot run on serverless: it holds a persistent
OKX WebSocket session and serves its own WSS `/events` stream.

Status convention in this file: everything the repository ships today is
marked **[shipped]**. Anything that requires an actual hosting decision is
marked **[operator]** — those steps need a provider account/credentials and
cannot be performed from inside the repo alone.

## 1. Engine image [shipped]

`server/Dockerfile` already satisfies the long-running service contract:

- multi-stage build (`gcc:bookworm` → `debian:bookworm-slim`), static
  libstdc++/libgcc so the runtime GLIBCXX matches the slim base
- `ca-certificates` + `libssl3` present → outbound `wss://` to OKX works
- non-root user `qx`, no toolchain in the runtime layer
- binds `0.0.0.0`, respects the host-provided `PORT`
- graceful SIGINT/SIGTERM shutdown (HTTP stop → feed stop → clean exit)
- `HEALTHCHECK` against `/health`

Build:

```bash
docker build -f server/Dockerfile -t qx-engine .
```

## 2. Engine environment [operator applies these values]

| Variable | Required value / example | Notes |
|---|---|---|
| `QX_MODE` | `live` | replay mode is for local demo only |
| `QX_EXCHANGE` | `okx` | only okx is implemented |
| `QX_INSTRUMENT` | `BTC-USDT` | any OKX v5 spot instId |
| `QX_CHANNEL` | `books` | L2 depth channel |
| `QX_OKX_WS_URL` | `wss://ws.okx.com:8443/ws/v5/public` | regional endpoints exist; OKX rotates them for maintenance |
| `QX_ALLOWED_ORIGINS` | `https://<dashboard-domain>` | exact Vercel origin(s), comma-separated; no blanket `*` |
| `PORT` | host-injected | never hardcode another value on PaaS hosts |
| `QX_CA_CERT_PATH` | unset unless the host needs a custom CA bundle | empty = system store |
| `QX_STALENESS_MS` | `5000` (default) | books can be quiet briefly; keep ≥5000 |

Run on any Docker-capable host:

```bash
docker run -d --name quantxecute-engine \
  --restart unless-stopped \
  -p 443:8080 \                # or behind your TLS proxy / platform router
  -e QX_MODE=live \
  -e QX_EXCHANGE=okx \
  -e QX_INSTRUMENT=BTC-USDT \
  -e QX_CHANNEL=books \
  -e QX_OKX_WS_URL=wss://ws.okx.com:8443/ws/v5/public \
  -e QX_ALLOWED_ORIGINS=https://<dashboard-domain> \
  qx-engine
```

TLS termination is the host's job (platform edge, Caddy/nginx, or a tunnel).
The dashboard enforces HTTPS/WSS browser-side.

## 3. Health contract to verify after deployment

`GET /health` must show, meaningfully:

- `mode:"live"`, `exchange:"okx"`, `instrument:"BTC-USDT"`, `channel:"books"`
- `connected:true`, `bookReady:true`, `stale:false`
- counters present: `sequenceGaps`, `reconnects`, `malformedMessages`,
  `lastMessageAgeMs`, `lastSeqId`

`GET /book`: non-empty bids/asks, `sequence > 0`, sequence advancing between
polls. `POST /simulate` with a small BUY and SELL must return structured real
execution results computed against the live book (never fixture fallback —
live mode refuses simulation with 503 while the book is not ready).

Verified locally against real OKX through this exact image on 2026-08-24:
snapshot + incrementals with zero gaps/malformed frames, BUY/SELL simulations
executed against the live ladder, container restart recovered
(resubscribe → snapshot → bookReady) including surviving a mid-session
1006 abnormal closure via FeedClient backoff.

## 4. Restart / recovery expectations

The engine reconnects automatically: transport drops retire the worker
session and FeedClient re-connects with exponential backoff (capped), then
resubscribes and re-anchors on a fresh snapshot. A container restart must
return to `bookReady:true` within seconds-to-tens-of-seconds. If a hosting
platform sleeps/restarts containers, this is the behavior that matters;
verify once after deployment by restarting the container and watching
`/health`.

Note: OKX rate-limits connection attempts per IP. Avoid tight
restart loops; normal orchestrator restarts are fine.

## 5. Dashboard on Vercel [shipped config / operator owns secrets]

Project settings → Environment Variables:

```
NEXT_PUBLIC_ENGINE_HTTP_URL=https://<engine-host>
NEXT_PUBLIC_ENGINE_WS_URL=wss://<engine-host>
```

- No `localhost` values in production.
- The committed `dashboard/package-lock.json` pins dependencies; Vercel's
  npm install resolves the same tree CI verifies with `npm ci`.
- When the engine host changes, update both values and redeploy.

CORS: keep `QX_ALLOWED_ORIGINS` on the engine equal to the exact Vercel
production origin. Local development stays separate (localhost origins are
allowed for dev only).
