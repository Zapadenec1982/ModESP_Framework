# components/modesp_scenario/tests/host/Makefile
# Standalone host build для scenario engine unit tests.
#
# Phase 1 scope: test_action_registry. Additional tests added у Phase 1.5 as
# they're ported from old modesp_sequence/tests/host/ з new IStateBackend
# и DI-injected ActionRegistry/ContinuousRegistry signatures.
#
# Usage:
#     make            # build all tests
#     make test       # build + run all tests
#     make clean      # remove build artifacts

# ── Compiler ──
CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wno-unused-parameter -Wno-unused-variable \
           -DETL_NO_PROFILE_HEADER -DHOST_BUILD=1 -O0 -g

# ── Paths ──
THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
COMPONENT := $(abspath $(THIS_DIR)/../..)
ROOT := $(abspath $(COMPONENT)/../..)
BUILD := $(THIS_DIR)build

# ── ETL include path discovery (same pattern as old modesp_sequence) ──
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
    -I$(COMPONENT)/private \
    -I$(THIS_DIR)common \
    -I$(ROOT)/components/modesp_core/include \
    -I$(ETL_INCLUDE)

# ── Sources ──
REG_SRCS := $(COMPONENT)/src/actions/action_registry.cpp

# ── Test targets ──
EXEEXT :=
ifeq ($(OS),Windows_NT)
    EXEEXT := .exe
endif

TESTS := test_action_registry
TEST_BINS := $(addprefix $(BUILD)/,$(addsuffix $(EXEEXT),$(TESTS)))

# ── Default target ──
.PHONY: all test clean info
all: $(TEST_BINS)

$(BUILD)/test_action_registry$(EXEEXT): $(THIS_DIR)test_action_registry.cpp $(REG_SRCS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(REG_SRCS)

$(BUILD):
	@mkdir -p $(BUILD)

test: all
	@echo ""
	@echo "── Running scenario engine host tests ──"
	@for t in $(TEST_BINS); do \
	    echo ""; echo "= $$t ="; \
	    $$t || { echo "FAIL: $$t"; exit 1; }; \
	done
	@echo ""
	@echo "All tests PASSED"

clean:
	@rm -rf $(BUILD)

info:
	@echo "ETL_INCLUDE  = $(ETL_INCLUDE)"
	@echo "ROOT         = $(ROOT)"
	@echo "COMPONENT    = $(COMPONENT)"
	@echo "TESTS        = $(TESTS)"
