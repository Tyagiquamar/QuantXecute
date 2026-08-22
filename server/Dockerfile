FROM gcc:bookworm AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY CMakeLists.txt ./
COPY cmake cmake
COPY core core
COPY feed feed
COPY server server
COPY third_party third_party

RUN cmake -S . -B build-server -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DQX_BUILD_TESTS=OFF -DQX_BUILD_BENCH=OFF -DQX_BUILD_CONSOLE=OFF \
    && cmake --build build-server --target quantxecute-server

FROM gcc:bookworm

WORKDIR /engine
COPY --from=build /work/build-server/server/quantxecute-server ./quantxecute-server
COPY core/fixtures ./fixtures

EXPOSE 8080
ENTRYPOINT ["./quantxecute-server"]
