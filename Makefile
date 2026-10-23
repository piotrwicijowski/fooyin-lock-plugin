.DEFAULT_GOAL := help

PLUGIN_BUILD_DIR ?= build
PLUGIN_RELEASE_BUILD_DIR ?= build-release
PLUGIN_TEST_BUILD_DIR ?= build-test

ifneq (,$(wildcard /fooyin/CMakeLists.txt))
FOOYIN_SRC_DIR ?= /fooyin
else
FOOYIN_SRC_DIR ?= $(abspath ../fooyin)
endif

FOOYIN_BUILD_DIR ?= $(FOOYIN_SRC_DIR)/build
FOOYIN_SDK_DIR ?= $(FOOYIN_BUILD_DIR)/sdk
FOOYIN_PLUGIN_DIR ?= $(FOOYIN_BUILD_DIR)/run/lib/fooyin/plugins
# Evaluate at launch time, including after a build in the same make invocation.
FOOYIN_BIN ?= $(shell if test -x "$(FOOYIN_BUILD_DIR)/run/fooyin"; then printf '%s' "$(FOOYIN_BUILD_DIR)/run/fooyin"; elif test -x "$(FOOYIN_BUILD_DIR)/run/bin/fooyin"; then printf '%s' "$(FOOYIN_BUILD_DIR)/run/bin/fooyin"; elif test -x "$(FOOYIN_SDK_DIR)/bin/fooyin"; then printf '%s' "$(FOOYIN_SDK_DIR)/bin/fooyin"; fi)

CMAKE_GENERATOR ?= Ninja
CMAKE_ARGS ?=
FOOYIN_CMAKE_ARGS ?= -DBUILD_TRANSLATIONS=OFF
TEST_ARGS ?= --output-on-failure
QT_LOGGING_RULES ?= fy.sessionlock.debug=true

# The lock plugin requires installed public headers and an exported Fooyin SDK.
FOOYIN_CMAKE_PATHS = -DCMAKE_PREFIX_PATH="$(FOOYIN_SDK_DIR)"
CONFIG_ENV = $(if $(strip $(XDG_CONFIG_HOME)),XDG_CONFIG_HOME="$(XDG_CONFIG_HOME)")

.PHONY: help fooyin-configure fooyin-build fooyin-install configure configure-release \
    configure-tests build build-release install-plugin sync-plugin install-plugin-release \
    run run-debug run-gdb dev dev-release test check

help:
	@printf '%s\n' \
	  'Session Lock (Linux development convenience targets):' \
	  '  fooyin-configure               Configure fooyin with installed SDK headers enabled' \
	  '  fooyin-build / fooyin-install   Build fooyin or stage its runtime and SDK' \
	  '  configure / build              Configure and build Debug' \
	  '  configure-release / build-release  Configure and build RelWithDebInfo' \
	  '  install-plugin / sync-plugin   Build, then copy only fyplugin_lock.so' \
	  '  dev / dev-release              Build fooyin, stage SDK, configure/build/copy plugin' \
	  '  configure-tests / test / check Build and run tests with Qt offscreen' \
	  '  run / run-debug                Launch fooyin (debug enables fy.sessionlock)' \
	  '  run-gdb                        Launch fooyin under GDB with debug logging' \
	  '' \
	  'First sibling-tree build: make fooyin-configure && make dev' \
	  'Existing SDK: make build FOOYIN_SDK_DIR=/path/to/fooyin-sdk' \
	  '' \
	  'Overrides: PLUGIN_BUILD_DIR, PLUGIN_RELEASE_BUILD_DIR, PLUGIN_TEST_BUILD_DIR,' \
	  '  FOOYIN_SRC_DIR, FOOYIN_BUILD_DIR, FOOYIN_SDK_DIR, FOOYIN_PLUGIN_DIR, FOOYIN_BIN,' \
	  '  CMAKE_GENERATOR, CMAKE_ARGS, FOOYIN_CMAKE_ARGS, TEST_ARGS, QT_LOGGING_RULES,' \
	  '  XDG_CONFIG_HOME. Pass extra CMake options using CMAKE_ARGS.'

fooyin-configure:
	cmake -S "$(FOOYIN_SRC_DIR)" -B "$(FOOYIN_BUILD_DIR)" -G "$(CMAKE_GENERATOR)" $(FOOYIN_CMAKE_ARGS) -DINSTALL_HEADERS=ON

fooyin-build:
	cmake --build "$(FOOYIN_BUILD_DIR)"

fooyin-install:
	cmake --install "$(FOOYIN_BUILD_DIR)" --prefix "$(FOOYIN_SDK_DIR)"

configure:
	cmake -S . -B "$(PLUGIN_BUILD_DIR)" -G "$(CMAKE_GENERATOR)" -DCMAKE_BUILD_TYPE=Debug $(FOOYIN_CMAKE_PATHS) $(CMAKE_ARGS) -DBUILD_TESTING=OFF

configure-release:
	cmake -S . -B "$(PLUGIN_RELEASE_BUILD_DIR)" -G "$(CMAKE_GENERATOR)" -DCMAKE_BUILD_TYPE=RelWithDebInfo $(FOOYIN_CMAKE_PATHS) $(CMAKE_ARGS) -DBUILD_TESTING=OFF

configure-tests:
	cmake -S . -B "$(PLUGIN_TEST_BUILD_DIR)" -G "$(CMAKE_GENERATOR)" -DCMAKE_BUILD_TYPE=Debug $(FOOYIN_CMAKE_PATHS) $(CMAKE_ARGS) -DBUILD_TESTING=ON

build: configure
	cmake --build "$(PLUGIN_BUILD_DIR)"

build-release: configure-release
	cmake --build "$(PLUGIN_RELEASE_BUILD_DIR)" --config RelWithDebInfo

install-plugin: build
	install -d "$(FOOYIN_PLUGIN_DIR)"
	install -m 755 "$(PLUGIN_BUILD_DIR)/fyplugin_lock.so" "$(FOOYIN_PLUGIN_DIR)/fyplugin_lock.so"

sync-plugin: install-plugin

install-plugin-release: build-release
	install -d "$(FOOYIN_PLUGIN_DIR)"
	install -m 755 "$(PLUGIN_RELEASE_BUILD_DIR)/fyplugin_lock.so" "$(FOOYIN_PLUGIN_DIR)/fyplugin_lock.so"

# Recursive invocations keep SDK staging and plugin builds ordered under make -j.
dev: fooyin-build
	$(MAKE) fooyin-install
	$(MAKE) install-plugin

dev-release: fooyin-build
	$(MAKE) fooyin-install
	$(MAKE) install-plugin-release

test: configure-tests
	cmake --build "$(PLUGIN_TEST_BUILD_DIR)"
	env QT_QPA_PLATFORM=offscreen ctest --test-dir "$(PLUGIN_TEST_BUILD_DIR)" $(TEST_ARGS)

check: build test

run:
	@test -n "$(FOOYIN_BIN)" && test -x "$(FOOYIN_BIN)" || { printf '%s\n' 'No fooyin executable found. Build fooyin or set FOOYIN_BIN=/path/to/fooyin.' >&2; exit 1; }
	env $(CONFIG_ENV) "$(FOOYIN_BIN)"

run-debug:
	@test -n "$(FOOYIN_BIN)" && test -x "$(FOOYIN_BIN)" || { printf '%s\n' 'No fooyin executable found. Build fooyin or set FOOYIN_BIN=/path/to/fooyin.' >&2; exit 1; }
	env $(CONFIG_ENV) QT_LOGGING_RULES="$(QT_LOGGING_RULES)" "$(FOOYIN_BIN)"

run-gdb:
	@test -n "$(FOOYIN_BIN)" && test -x "$(FOOYIN_BIN)" || { printf '%s\n' 'No fooyin executable found. Build fooyin or set FOOYIN_BIN=/path/to/fooyin.' >&2; exit 1; }
	@command -v gdb >/dev/null 2>&1 || { printf '%s\n' 'GDB not found. Install gdb to use run-gdb.' >&2; exit 1; }
	env $(CONFIG_ENV) QT_LOGGING_RULES="$(QT_LOGGING_RULES)" gdb -ex 'set pagination off' -ex run --args "$(FOOYIN_BIN)" --skip-single
