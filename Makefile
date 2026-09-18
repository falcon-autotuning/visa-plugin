.PHONY: help configure build test clean install vcpkg-bootstrap docker-up docker-down

# Build preset (user can override: make build PRESET=linux-gcc-release)
PRESET ?= linux-clang-release
CMAKE_BUILD_DIR := build/$(PRESET)

help:
	@echo "visa-plugin Build System"
	@echo "============================="
	@echo ""
	@echo "Available presets:"
	@cmake --list-presets=all
	@echo ""
	@echo "Usage:"
	@echo "  make configure PRESET=<preset>  - Configure build (default: $(PRESET))"
	@echo "  make build PRESET=<preset>      - Build (default: $(PRESET))"
	@echo "  make test PRESET=<preset>       - Run tests with Docker (default: $(PRESET))"
	@echo "  make install PRESET=<preset>    - Install to system"
	@echo "  make clean                      - Clean all build artifacts"
	@echo ""
	@echo "Examples:"
	@echo "  make build                                      # Build with clang (default)"
	@echo "  make build PRESET=linux-gcc-release             # Build with gcc"
	@echo "  make test PRESET=linux-clang-release            # Run tests"
	@echo "  make install PRESET=linux-clang-release         # Install"

vcpkg-bootstrap:
	@echo "Bootstrapping vcpkg..."
	cmake -P cmake/bootstrap/bootstrap-vcpkg.cmake

configure: vcpkg-bootstrap
	@echo "Configuring $(PRESET)..."
	cmake --preset $(PRESET)

build: configure
	@echo "Building $(PRESET)..."
	cmake --build --preset $(PRESET)

.PHONY: setup-serial-test cleanup-serial-test
setup-serial-test:
	@echo "Setting up tty0tty serial test environment..."
	sudo modprobe tty0tty
	sudo chmod 666 /dev/tnt0 /dev/tnt1
	sudo ln -sf /dev/tnt0 /dev/ttyUSB0
	@echo "Serial simulator ready:"
	@echo "  VISA side:      /dev/ttyUSB0"
	@echo "  Simulator side: /dev/tnt1"

cleanup-serial-test:
	@echo "Cleaning up tty0tty serial test environment..."
	sudo rm -f /dev/ttyUSB0
	-sudo chmod 660 /dev/tnt0 /dev/tnt1
	-sudo modprobe -r tty0tty
	@echo "Serial simulator cleanup complete"

check-serial-test:
	@ls -l /dev/tnt0
	@ls -l /dev/tnt1
	@ls -l /dev/ttyUSB0

test: setup-serial-test build
	ctest --preset $(PRESET) -V
	$(MAKE) cleanup-serial-test

install: build
	@echo "Installing $(PRESET) to system..."
	cmake --install $(CMAKE_BUILD_DIR)

clean:
	@echo "Cleaning all build artifacts..."
	rm -rf build vcpkg_installed
	@echo "✓ Clean complete"
