CMAKE ?= cmake
QX_IMAGE ?= qx-buildtools

.PHONY: help verify bench clean docker-image

help:
	@echo "QuantXecute - real-time market-data and execution-simulation engine"
	@echo "  make docker-image  build the Linux verification image (gcc + cmake + ninja)"
	@echo "  make verify        full gate: ASan+UBSan build and ctest in the Linux image"
	@echo "  make bench         run benchmark suite (after instrumentation phase)"
	@echo "  make clean         remove local build directories"

docker-image:
	docker build -t $(QX_IMAGE) -f docker/build-tools.Dockerfile docker

verify: docker-image
	docker run --rm -v "$(CURDIR)":/work -w /work $(QX_IMAGE) \
		bash -ec 'cmake -S . -B build-verify -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQX_BUILD_TESTS=ON -DQX_SANITIZE_ADDRESS=ON -DQX_SANITIZE_UNDEFINED=ON && cmake --build build-verify -j $$(nproc) && cd build-verify && ctest --output-on-failure'

bench:
	@echo "bench targets arrive with the instrumentation phase"

clean:
	rm -rf build build-verify
