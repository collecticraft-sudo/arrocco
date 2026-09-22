# SPDX-License-Identifier: GPL-3.0-or-later
# Tests for the Lichess client.
#
#   make test        the offline tests (always), then the live one (skipped without a token)
#   make offline     the offline tests alone: no network, ever
#   make live        the live test alone
#
# The live test needs a Lichess token in ARROCCO_LICHESS_TOKEN or in
# ~/.config/arrocco/lichess-token. live_run.py starts sim/server.py --proxy-only around it, so
# the token stays in python: neither this Makefile nor the C++ ever sees it.
CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -g -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wshadow
ROOT     := ../..
CORE     := $(ROOT)/lib/arrocco/src
BUILD    := build

CHESS_SRC   := $(wildcard $(CORE)/arrocco/chess/*.cpp)
LICHESS_SRC := $(wildcard $(CORE)/arrocco/lichess/*.cpp)
HOST_SRC    := $(ROOT)/sim/host/lichess_transport.cpp
HEADERS     := $(wildcard $(CORE)/arrocco/lichess/*.h)
DEPS        := $(CHESS_SRC) $(LICHESS_SRC) $(HEADERS) $(wildcard $(CORE)/arrocco/chess/*.h)

.PHONY: test offline live headers clean

test: offline live

offline: headers $(BUILD)/unit $(BUILD)/attack
	@echo "== $(BUILD)/unit"
	@./$(BUILD)/unit
	@echo "== $(BUILD)/attack"
	@./$(BUILD)/attack

live: $(BUILD)/live
	@python3 live_run.py ./$(BUILD)/live

# Every header must compile when it is the only thing included.
headers:
	@set -e; for h in $(notdir $(HEADERS)); do \
	  echo "#include \"arrocco/lichess/$$h\"" | $(CXX) $(CXXFLAGS) -Werror -I$(CORE) -x c++ -fsyntax-only -; \
	done; echo "== headers: $(words $(HEADERS)) compile alone"

$(BUILD)/unit: unit_main.cpp fake_transport.h $(DEPS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(CORE) -I. unit_main.cpp $(CHESS_SRC) $(LICHESS_SRC) -o $@

# The adversarial suite: malformed payloads, torn lines, a server that disagrees with us.
$(BUILD)/attack: attack_main.cpp fake_transport.h $(DEPS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(CORE) -I. attack_main.cpp $(CHESS_SRC) $(LICHESS_SRC) -o $@

$(BUILD)/live: live_main.cpp $(HOST_SRC) $(ROOT)/sim/host/lichess_transport.h $(DEPS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(CORE) -I$(ROOT)/sim/host live_main.cpp $(CHESS_SRC) $(LICHESS_SRC) $(HOST_SRC) -o $@ -lpthread

clean:
	rm -rf $(BUILD)
