QX_IMAGE ?= qx-buildtools

DOCKER_RUN = docker run --rm -v "$(CURDIR)":/work -w /work $(QX_IMAGE)

# TSan needs a permissive seccomp profile; docker flags must precede the image.
TSAN_DOCKER_RUN = docker run --rm --security-opt seccomp=unconfined -v "$(CURDIR)":/work -w /work $(QX_IMAGE)

.PHONY: help docker-image verify verify-fast verify-tsan verify-tidy bench clean

help:
	@echo "QuantXecute - real-time market-data and execution-simulation engine"
	@echo "  make docker-image  build the Linux verification image (gcc + cmake + ninja + clang-tidy)"
	@echo "  make verify        full gate: ASan+UBSan ctest, TSan pass, clang-tidy"
	@echo "  make verify-fast   ASan+UBSan build and ctest only"
	@echo "  make verify-tsan   ThreadSanitizer pass over the suite"
	@echo "  make verify-tidy   clang-tidy over engine sources"
	@echo "  make bench         Release-mode benchmarks (prints percentiles)"
	@echo "  make clean         remove local build directories"

docker-image:
	docker build -t $(QX_IMAGE) -f docker/build-tools.Dockerfile docker

verify-fast: docker-image
	$(DOCKER_RUN) bash -ec '\
		cmake -S . -B build-verify -G Ninja -DCMAKE_BUILD_TYPE=Debug \
			-DQX_BUILD_TESTS=ON -DQX_BUILD_BENCH=OFF \
			-DQX_SANITIZE_ADDRESS=ON -DQX_SANITIZE_UNDEFINED=ON && \
		cmake --build build-verify && \
		cd build-verify && ctest --output-on-failure --timeout 120'

verify-tsan: docker-image
	$(TSAN_DOCKER_RUN) bash -ec '\
		cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
			-DQX_BUILD_TESTS=ON \
			-DQX_SANITIZE_THREAD=ON && \
		cmake --build build-tsan && \
		cd build-tsan && ctest --output-on-failure --timeout 120'

verify-tidy: docker-image
	$(DOCKER_RUN) bash -ec '\
		cmake -S . -B build-tidy -G Ninja -DCMAKE_BUILD_TYPE=Debug \
			-DCMAKE_CXX_COMPILER=g++-12 \
			-DQX_BUILD_TESTS=OFF -DQX_BUILD_SERVER=ON && \
		cmake --build build-tidy && \
		clang-tidy -p build-tidy --warnings-as-errors="*" core/src/*.cpp feed/src/*.cpp server/src/*.cpp'

verify: verify-fast verify-tsan verify-tidy

bench: docker-image
	$(DOCKER_RUN) bash -ec '\
		cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release \
			-DQX_BUILD_TESTS=OFF -DQX_BUILD_BENCH=ON && \
		cmake --build build-bench && \
		echo "=== book_bench ===" && ./build-bench/core/bench/book_bench && \
		echo "=== replay_bench ===" && ./build-bench/core/bench/replay_bench'

clean:
	rm -rf build-verify build-tsan build-tidy build-bench
