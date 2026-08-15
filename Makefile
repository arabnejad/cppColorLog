CMAKE ?= cmake
BUILD_DIR ?= build
BUILD_TYPE ?= Debug
JOBS ?= 2

.DEFAULT_GOAL := all

.PHONY: all configure build examples run-examples tests test run-tests format install clean help

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCPPCOLORLOGGER_BUILD_EXAMPLES=OFF \
		-DCPPCOLORLOGGER_BUILD_TESTS=OFF

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)

examples:
	$(CMAKE) -S . -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCPPCOLORLOGGER_BUILD_EXAMPLES=ON \
		-DCPPCOLORLOGGER_BUILD_TESTS=OFF
	$(CMAKE) --build $(BUILD_DIR) --target examples --parallel $(JOBS)

run-examples:
	$(CMAKE) -S . -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCPPCOLORLOGGER_BUILD_EXAMPLES=ON \
		-DCPPCOLORLOGGER_BUILD_TESTS=OFF
	$(CMAKE) --build $(BUILD_DIR) --target run_all_samples --parallel $(JOBS)

tests:
	$(CMAKE) -S . -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCPPCOLORLOGGER_BUILD_EXAMPLES=OFF \
		-DCPPCOLORLOGGER_BUILD_TESTS=ON \
		-DBUILD_TESTING=ON
	$(CMAKE) --build $(BUILD_DIR) --target logger_tests --parallel $(JOBS)

test: tests
	$(CMAKE) --build $(BUILD_DIR) --target run_tests

run-tests: test

format: configure
	$(CMAKE) --build $(BUILD_DIR) --target clang_format

install: build
	$(CMAKE) --build $(BUILD_DIR) --target install

clean:
	@found_build=false; \
	for build_dir in "$(BUILD_DIR)" build build-*; do \
		if [ ! -f "$$build_dir/CMakeCache.txt" ]; then \
			continue; \
		fi; \
		cache_source_dir=$$(sed -n 's|^CMAKE_HOME_DIRECTORY:INTERNAL=||p' "$$build_dir/CMakeCache.txt"); \
		if [ "$$cache_source_dir" != "$(CURDIR)" ]; then \
			$(CMAKE) -E echo "Skipping $$build_dir: it belongs to a different project"; \
			continue; \
		fi; \
		found_build=true; \
		$(CMAKE) -E echo "Removing $$build_dir"; \
		$(CMAKE) -E remove_directory "$$build_dir"; \
	done; \
	if [ "$$found_build" = false ]; then \
		$(CMAKE) -E echo "No cppColorLogger build directories found"; \
	fi

help:
	@$(CMAKE) -E echo "make                 Configure and build the library"
	@$(CMAKE) -E echo "make configure       Configure the CMake build"
	@$(CMAKE) -E echo "make examples        Build all examples"
	@$(CMAKE) -E echo "make run-examples    Build and run all examples"
	@$(CMAKE) -E echo "make tests           Build unit tests"
	@$(CMAKE) -E echo "make test            Build and run unit tests"
	@$(CMAKE) -E echo "make format          Format source files"
	@$(CMAKE) -E echo "make install         Install through CMake"
	@$(CMAKE) -E echo "make clean           Remove this project's build/ and build-* directories"
	@$(CMAKE) -E echo "Variables: BUILD_DIR, BUILD_TYPE, JOBS, CMAKE"
