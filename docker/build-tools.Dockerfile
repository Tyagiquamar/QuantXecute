FROM gcc:bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake ninja-build ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
