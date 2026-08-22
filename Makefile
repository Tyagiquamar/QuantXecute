CMAKE ?= cmake
BUILD_DIR ?= build
BUILD_TYPE ?= Debug

QX_IMAGE ?= qx-buildtools

.PHONY: help configure build test verify bench clean docker-image

help:
	@echo "QuantXecute - real-time market-data and execution-simulation engine"
	@echo "  make configure    configure local build"
	@echo "  make test         build + ctest locally (no sanitizers)"
	@echo "  make docker-image build the Linux verification image (gcc + cmake + ninja)"
	@echo "  make verify       full gate: ASan+UBSan build and ctest in the Linux image"
	@echo "  make bench        run benchmark suite (after instrumentation phase)"
	@echo "  make clean        remove local build directory"

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DQX_BUILD_TESTS=ON

build:
	$(CMAKE) --build $(BUILD_DIR)

test:
	$(CMAKE) --build $(BUILD_DIR)
	cd $(BUILD_DIR) && $(CMAKE) -E ctest --output-on-failure

docker-image:
	docker build -t $(QX_IMAGE) -f docker/build-tools.Dockerfile docker

verify: docker-image
	docker run --rm -v "$(CURDIR)":/work -w /work $(QX_IMAGE) \
		bash -ec 'cmake -S . -B build-verify -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQX_BUILD_TESTS=ON -DQX_SANITIZE_ADDRESS=ON -DQX_SANITIZE_UNDEFINED=ON && cmake --build build-verify -j $$(nproc) && cd build-verify && ctest --output-on-failure'

bench:
	@echo "bench targets arrive with the instrumentation phase"

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR) build-verify
