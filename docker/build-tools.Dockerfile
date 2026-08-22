FROM gcc:bookworm

# libssl-dev enables TLS-capable IXWebSocket builds used by the server and
# the live-smoke tool; git is required by CMake FetchContent.
RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake ninja-build clang-tidy libssl-dev ca-certificates git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work