# SPDX-License-Identifier: GPL-3.0-or-later
# Native tests for the chess rules library. `make test` builds and runs everything.
CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic
ROOT     := ../..
SRC      := $(wildcard $(ROOT)/lib/arrocco/src/arrocco/chess/*.cpp)
BUILD    := build
TESTS    := $(patsubst %_main.cpp,$(BUILD)/%,$(wildcard *_main.cpp))

.PHONY: test clean
test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$$t; done

$(BUILD)/%: %_main.cpp $(SRC) $(wildcard $(ROOT)/lib/arrocco/src/arrocco/chess/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(ROOT)/lib/arrocco/src $< $(SRC) -o $@

clean:
	rm -rf $(BUILD)
