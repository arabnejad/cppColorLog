CMAKE ?= cmake
BUILD_DIR ?= build
BUILD_TYPE ?= Debug
JOBS ?= 2

.DEFAULT_GOAL := all

.PHONY: all configure build examples run-examples tests test run-tests format install clean help

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)

examples: configure
	$(CMAKE) --build $(BUILD_DIR) --target examples --parallel $(JOBS)

run-examples: configure
	$(CMAKE) --build $(BUILD_DIR) --target run_all_samples --parallel $(JOBS)

tests: configure
	$(CMAKE) --build $(BUILD_DIR) --target logger_tests --parallel $(JOBS)

test: tests
	$(CMAKE) --build $(BUILD_DIR) --target run_tests

run-tests: test

format: configure
	$(CMAKE) --build $(BUILD_DIR) --target clang_format

install: build
	$(CMAKE) --install $(BUILD_DIR)

clean:
	@if [ -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		$(CMAKE) --build $(BUILD_DIR) --target clean; \
	else \
		$(CMAKE) -E echo "Nothing to clean in $(BUILD_DIR)"; \
	fi

help:
	@$(CMAKE) -E echo "make                 Configure and build everything"
	@$(CMAKE) -E echo "make configure       Configure the CMake build"
	@$(CMAKE) -E echo "make examples        Build all examples"
	@$(CMAKE) -E echo "make run-examples    Build and run all examples"
	@$(CMAKE) -E echo "make tests           Build unit tests"
	@$(CMAKE) -E echo "make test            Build and run unit tests"
	@$(CMAKE) -E echo "make format          Format source files"
	@$(CMAKE) -E echo "make install         Install through CMake"
	@$(CMAKE) -E echo "make clean           Clean CMake build outputs"
	@$(CMAKE) -E echo "Variables: BUILD_DIR, BUILD_TYPE, JOBS, CMAKE"
