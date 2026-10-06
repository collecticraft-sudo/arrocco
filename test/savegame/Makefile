# SPDX-License-Identifier: GPL-3.0-or-later
# Native tests for the saved game (lib/arrocco/src/arrocco/ui/saved_game.*): the round
# trip of every kind of game, and every way a blob gets refused. `make test` builds and
# runs everything.
CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wshadow
ROOT     := ../..
LIB      := $(ROOT)/lib/arrocco/src
CHESS    := $(wildcard $(LIB)/arrocco/chess/*.cpp)
# The format and the clock it keeps: nothing that draws, so no Adafruit GFX here.
UI       := $(LIB)/arrocco/ui/saved_game.cpp $(LIB)/arrocco/ui/clock.cpp
SRC      := $(CHESS) $(UI)
HEADERS  := $(wildcard $(LIB)/arrocco/chess/*.h) $(LIB)/arrocco/ui/saved_game.h $(LIB)/arrocco/ui/clock.h \
            $(LIB)/arrocco/ui/screen.h $(LIB)/arrocco/engine.h $(LIB)/arrocco/platform.h
BUILD    := build
TESTS    := $(patsubst %_main.cpp,$(BUILD)/%,$(wildcard *_main.cpp))

.PHONY: test headers sanitize clean
test: headers $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$$t; done

# The saved game's header must compile when it is the only thing included.
headers:
	@echo '#include "arrocco/ui/saved_game.h"' | $(CXX) $(CXXFLAGS) -Werror -I$(LIB) -x c++ -fsyntax-only -; \
	echo "== headers: saved_game.h compiles alone"

$(BUILD)/%: %_main.cpp $(SRC) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(LIB) $< $(SRC) -o $@

# The same runner under UBSan (not part of `test`). Where AddressSanitizer works, add it:
#   make sanitize SANITIZERS=address,undefined
SANITIZERS ?= undefined
sanitize:
	@mkdir -p $(BUILD)/san
	@set -e; for t in $(patsubst %_main.cpp,%,$(wildcard *_main.cpp)); do \
	  $(CXX) -std=c++17 -O1 -g -fno-exceptions -fno-rtti -fsanitize=$(SANITIZERS) -fno-sanitize-recover=undefined \
	    -Wall -Wextra -Wpedantic -I$(LIB) $${t}_main.cpp $(SRC) -o $(BUILD)/san/$$t; \
	  echo "== $(BUILD)/san/$$t"; ./$(BUILD)/san/$$t; \
	done

clean:
	rm -rf $(BUILD)
