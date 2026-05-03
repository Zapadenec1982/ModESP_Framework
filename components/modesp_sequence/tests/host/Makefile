# components/modesp_sequence/tests/host/Makefile
# Standalone host build для sequence engine unit tests. Independent of
# project-level tests/host/CMakeLists.txt що entangled з stripped modules.
#
# Usage:
#     make                  # build all tests
#     make test             # build і run all tests
#     make clean            # remove build artifacts
#
# Tests use plain assert() — no doctest dependency.

# ── Compiler ──
CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wno-unused-parameter -Wno-unused-variable \
           -DETL_NO_PROFILE_HEADER -DHOST_BUILD=1 -O0 -g

# ── Paths ──
THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
COMPONENT := $(abspath $(THIS_DIR)/../..)
ROOT := $(abspath $(COMPONENT)/../..)
BUILD := $(THIS_DIR)build

# ── ETL include path discovery ──
# Try managed_components first (post-`idf.py build`), then global cache.
ETL_MANAGED := $(ROOT)/managed_components/marcel-cd__etlcpp/etl/include
ETL_CACHE_GLOB := $(wildcard $(LOCALAPPDATA)/Espressif/ComponentManager/Cache/*/marcel-cd__etlcpp_*/etl/include)
ETL_CACHE_GLOB_HOME := $(wildcard $(HOME)/.espressif/ComponentManager/Cache/*/marcel-cd__etlcpp_*/etl/include)

ifneq ("$(wildcard $(ETL_MANAGED))","")
    ETL_INCLUDE := $(ETL_MANAGED)
else ifneq ("$(ETL_CACHE_GLOB)","")
    ETL_INCLUDE := $(lastword $(sort $(ETL_CACHE_GLOB)))
else ifneq ("$(ETL_CACHE_GLOB_HOME)","")
    ETL_INCLUDE := $(lastword $(sort $(ETL_CACHE_GLOB_HOME)))
else
    $(error ETL not found. Run 'idf.py build' first OR install ETL system-wide)
endif

INCLUDES := \
    -I$(COMPONENT)/include \
    -I$(ROOT)/components/modesp_core/include \
    -I$(ETL_INCLUDE)

# Includes для tests що потребують SharedState (real implementation з host mocks).
# Adds project-level test mocks (freertos_mock.h, esp_log_mock.h, etc.) і
# generated/state_meta.h required by shared_state.h.
SS_INCLUDES := \
    $(INCLUDES) \
    -I$(ROOT)/tests/host \
    -I$(ROOT)/tests/host/mocks \
    -I$(ROOT)/generated

# ── Sources ──
COMPONENT_SRCS := $(COMPONENT)/src/action_registry.cpp $(COMPONENT)/src/continuous_registry.cpp
SS_HOST_SRC := $(ROOT)/tests/host/shared_state_host.cpp

# Sources for builtin_actions test (registry + builtins + real SharedState)
BUILTIN_SRCS := $(COMPONENT)/src/action_registry.cpp \
                $(COMPONENT)/src/builtin_actions.cpp \
                $(SS_HOST_SRC)

# Sources for modr_loader test (registry + builtins + loader + real SharedState).
# builtin_actions.cpp transitively requires SharedState; pull real impl з host
# mocks to satisfy linker.
LOADER_SRCS := $(COMPONENT)/src/action_registry.cpp \
               $(COMPONENT)/src/builtin_actions.cpp \
               $(COMPONENT)/src/modr_loader.cpp \
               $(SS_HOST_SRC)

# Sources for resource_arbiter test (no SharedState dependency).
ARBITER_SRCS := $(COMPONENT)/src/resource_arbiter.cpp

# Sources for track synchronization test (full engine stack)
SYNC_SRCS := $(COMPONENT)/src/action_registry.cpp \
             $(COMPONENT)/src/builtin_actions.cpp \
             $(COMPONENT)/src/modr_loader.cpp \
             $(COMPONENT)/src/resource_arbiter.cpp \
             $(COMPONENT)/src/sequence_track.cpp \
             $(COMPONENT)/src/sequence_instance.cpp \
             $(SS_HOST_SRC)

# Sources for sequence_engine test (full stack + engine class + nvs_token
# для persist_scan path).
ENGINE_SRCS := $(SYNC_SRCS) $(COMPONENT)/src/sequence_engine.cpp \
               $(COMPONENT)/src/nvs_token.cpp

# Sources for nvs_token test (full stack + nvs_token impl)
NVS_SRCS := $(SYNC_SRCS) $(COMPONENT)/src/nvs_token.cpp

# ── Test targets ──
TESTS := test_action_registry test_continuous_registry test_builtin_actions \
         test_modr_loader test_resource_arbiter test_track_synchronization \
         test_sequence_engine test_nvs_token

TEST_BINS := $(addprefix $(BUILD)/,$(addsuffix $(EXEEXT),$(TESTS)))
EXEEXT :=
ifeq ($(OS),Windows_NT)
    EXEEXT := .exe
endif

# ── Default target ──
.PHONY: all test clean
all: $(TEST_BINS)

# ── Per-test build rules ──
# Default rule (registry-only tests): action_registry + continuous_registry sources
$(BUILD)/test_action_registry$(EXEEXT): $(THIS_DIR)test_action_registry.cpp $(COMPONENT_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(COMPONENT_SRCS)

$(BUILD)/test_continuous_registry$(EXEEXT): $(THIS_DIR)test_continuous_registry.cpp $(COMPONENT_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(COMPONENT_SRCS)

# Builtin actions test: needs real SharedState + freertos/esp_log mocks
$(BUILD)/test_builtin_actions$(EXEEXT): $(THIS_DIR)test_builtin_actions.cpp $(BUILTIN_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SS_INCLUDES) -o $@ $< $(BUILTIN_SRCS)

# Loader test: registry + builtins + loader + real SharedState (deps via builtins)
$(BUILD)/test_modr_loader$(EXEEXT): $(THIS_DIR)test_modr_loader.cpp $(LOADER_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SS_INCLUDES) -o $@ $< $(LOADER_SRCS)

# ResourceArbiter test: standalone (no SharedState, no registry needed)
$(BUILD)/test_resource_arbiter$(EXEEXT): $(THIS_DIR)test_resource_arbiter.cpp $(ARBITER_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(ARBITER_SRCS)

# Track sync test: full engine stack + real SharedState; depends on fixture
# generated by tools/compile_scenario.py (run via pytest session fixture).
$(BUILD)/test_track_synchronization$(EXEEXT): $(THIS_DIR)test_track_synchronization.cpp $(SYNC_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SS_INCLUDES) -o $@ $< $(SYNC_SRCS)

# SequenceEngine integration test: full stack + engine class wrapping it
$(BUILD)/test_sequence_engine$(EXEEXT): $(THIS_DIR)test_sequence_engine.cpp $(ENGINE_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SS_INCLUDES) -o $@ $< $(ENGINE_SRCS)

# NVS token persistence test: full stack + nvs_token impl
$(BUILD)/test_nvs_token$(EXEEXT): $(THIS_DIR)test_nvs_token.cpp $(NVS_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SS_INCLUDES) -o $@ $< $(NVS_SRCS)

# ── Build dir ──
$(BUILD):
	@mkdir -p $(BUILD)

# ── Run tests ──
test: all
	@echo ""
	@echo "── Running sequence engine host tests ──"
	@for t in $(TEST_BINS); do \
	    echo ""; echo "= $$t ="; \
	    $$t || { echo "FAIL: $$t"; exit 1; }; \
	done
	@echo ""
	@echo "All tests PASSED"

clean:
	@rm -rf $(BUILD)

# ── Diagnostic ──
.PHONY: info
info:
	@echo "ETL_INCLUDE  = $(ETL_INCLUDE)"
	@echo "ROOT         = $(ROOT)"
	@echo "COMPONENT    = $(COMPONENT)"
	@echo "TESTS        = $(TESTS)"
