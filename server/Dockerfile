# ---- build stage -----------------------------------------------------------
FROM gcc:bookworm AS build

# libssl-dev: TLS backend for IXWebSocket (outbound wss:// to OKX).
RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake ninja-build libssl-dev git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY CMakeLists.txt ./
COPY cmake cmake
COPY core core
COPY feed feed
COPY server server
COPY third_party third_party

# Static libstdc++/libgcc: the gcc:bookworm image ships a newer GCC than the
# debian:bookworm-slim runtime's libstdc++6; the engine binary must not
# depend on GLIBCXX versions newer than the slim image provides.
RUN cmake -S . -B build-server -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DQX_BUILD_TESTS=OFF -DQX_BUILD_BENCH=OFF -DQX_BUILD_CONSOLE=OFF \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    && cmake --build build-server --target quantxecute-server quantxecute-live-smoke

# ---- runtime stage ---------------------------------------------------------
FROM debian:bookworm-slim

# CA certificates are required for outbound wss:// OKX connections;
# libssl3 for IXWebSocket; curl for the container HEALTHCHECK.
# No compiler or build toolchain ships in the production image.
RUN apt-get update \
    && apt-get install -y --no-install-recommends libssl3 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system qx && useradd --system --gid qx qx

WORKDIR /engine
COPY --from=build /work/build-server/server/quantxecute-server ./quantxecute-server
COPY --from=build /work/build-server/server/quantxecute-live-smoke ./quantxecute-live-smoke
COPY core/fixtures ./fixtures

ENV PORT=8080
EXPOSE 8080

USER qx

# Deterministic liveness proof for orchestrators: engine answers on /health.
HEALTHCHECK --interval=30s --timeout=5s --start-period=15s --retries=3 \
    CMD curl -fsS "http://127.0.0.1:${PORT}/health" || exit 1

ENTRYPOINT ["./quantxecute-server"]