# OSF CBPC — perf harness build (portable g++/clang fallback).
#
# The canonical build is xmake (see bench/xmake.lua), which also builds with
# MSVC for game-representative numbers. This Makefile exists so the harness can
# be built and run anywhere (Linux dev box, CI) with no toolchain setup, and so
# the codegen tiers from docs/PERF.md can be A/B'd by config:
#
#   make CONFIG=release   # ~ today's `mode.releasedbg`: -O2, no LTO   (baseline)
#   make CONFIG=lto       # Tier 1.1: -O2 -flto  (inlines the CLSF Ni* operators)
#   make CONFIG=avx2      # Tier 1.1+1.2+1.3: -flto -mavx2 -mfma -ffp-contract=fast
#   make CONFIG=fast      # -O3 -flto -march=native + the above
#
# Each config builds a distinct binary (build/bench-<config>) so they coexist:
#   make CONFIG=release && make CONFIG=avx2
#   ./build/bench-release && ./build/bench-avx2
#
# CXX=clang++ make ...    # to compare compilers.

ROOT    := ..
CXX     ?= g++
CONFIG  ?= release

SRC := \
	main.cpp \
	clsf_ops.cpp \
	$(ROOT)/src/Physics/JiggleSolver.cpp

INCLUDES := -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/lib/commonlibsf/include
COMMON   := -std=c++23 -pthread -Wall -Wextra -Wno-unused-parameter \
            -include $(ROOT)/bench/prelude.h $(INCLUDES)

FLAGS_release := -O2 -DNDEBUG
FLAGS_lto     := -O2 -flto -DNDEBUG
FLAGS_avx2    := -O2 -flto -mavx2 -mfma -ffp-contract=fast -DNDEBUG
FLAGS_fast    := -O3 -flto -march=native -mfma -ffp-contract=fast -funroll-loops -DNDEBUG
FLAGS_debug   := -O0 -g -fsanitize=address,undefined

CXXFLAGS := $(COMMON) $(FLAGS_$(CONFIG))

OUT := build/bench-$(CONFIG)

.PHONY: all run clean
all: $(OUT)

$(OUT): $(SRC) prelude.h bench_util.h solver_bench.h registry_bench.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT)
	@echo "built $(OUT)  (CONFIG=$(CONFIG), CXX=$(CXX))"

run: $(OUT)
	./$(OUT)

clean:
	rm -rf build
