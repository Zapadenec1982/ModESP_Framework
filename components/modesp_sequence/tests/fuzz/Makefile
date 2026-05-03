# components/modesp_sequence/tests/fuzz/Makefile
#
# libFuzzer harness build для modr_validate() — see fuzz_modr_loader.cpp.
# Requires Clang з libFuzzer і AddressSanitizer support (any modern Clang ≥ 12).
#
# Usage:
#     make                            # build ./fuzz_modr_loader
#     ./fuzz_modr_loader corpus/      # run на seed corpus
#     make clean                      # rm fuzz_modr_loader і generated artifacts
#
# Tip: keep corpus/ small (1-5 representative seeds). libFuzzer evolves
# corpus internally — committed seeds just bootstrap exploration.
#
# CI tips:
#  - Smoke (per PR): -max_total_time=60
#  - Nightly: -max_total_time=3600 -fork=N (N = CPU cores)
#  - Persist new corpus entries between CI runs (artifact storage).

CXX ?= clang++

# -fsanitize=fuzzer enables libFuzzer driver і tracing.
# AddressSanitizer catches OOB reads/writes що bypass loader bounds checks.
# UBSAN catches signed overflow, null deref, etc. — common у byte-format parsers.
CXXFLAGS = -std=c++17 -O1 -g \
           -fsanitize=fuzzer,address,undefined \
           -fno-omit-frame-pointer \
           -DETL_NO_PROFILE_HEADER -DHOST_BUILD=1

# ── Paths ──
THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
COMPONENT := $(abspath $(THIS_DIR)/../..)
ROOT := $(abspath $(COMPONENT)/../..)

# ETL discovery — same logic як unit-test Makefile
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
    -I$(ROOT)/tests/host \
    -I$(ROOT)/tests/host/mocks \
    -I$(ROOT)/generated \
    -I$(ETL_INCLUDE)

SRCS := \
    $(THIS_DIR)fuzz_modr_loader.cpp \
    $(COMPONENT)/src/action_registry.cpp \
    $(COMPONENT)/src/builtin_actions.cpp \
    $(COMPONENT)/src/modr_loader.cpp \
    $(ROOT)/tests/host/shared_state_host.cpp

TARGET := $(THIS_DIR)fuzz_modr_loader

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(SRCS)

clean:
	@rm -f $(TARGET) crash-* leak-* timeout-* oom-*
