# SPDX-License-Identifier: GPL-3.0-or-later
# Native tests for the offline puzzle pack, and for the puzzle mode built on it (the rating,
# the choice of puzzles, the progress blob, the session). `make test` builds and runs everything.
CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic
ROOT     := ../..
LIB      := $(ROOT)/lib/arrocco/src
CHESS    := $(wildcard $(LIB)/arrocco/chess/*.cpp)
PUZZLES  := $(wildcard $(LIB)/arrocco/puzzles/*.cpp)
SRC      := $(CHESS) $(PUZZLES)
# The reader on its own: the fuzz binary brings its own writable data arrays
# instead of the generated puzzle_data.cpp, so that one is left out of its link.
READER   := $(LIB)/arrocco/puzzles/puzzles.cpp
HEADERS  := $(wildcard $(LIB)/arrocco/puzzles/*.h)
# The puzzle mode's logic: no drawing, so no Adafruit GFX. saved_game.cpp brings crc32().
MODE     := $(LIB)/arrocco/ui/puzzle_progress.cpp $(LIB)/arrocco/ui/puzzle_session.cpp $(LIB)/arrocco/ui/saved_game.cpp
MODE_H   := $(LIB)/arrocco/ui/puzzle_progress.h $(LIB)/arrocco/ui/puzzle_session.h $(LIB)/arrocco/ui/saved_game.h
BUILD    := build
TESTS    := $(BUILD)/pack $(BUILD)/fuzz $(BUILD)/session

.PHONY: test headers sanitize clean
FUZZ_STRIDE ?= 1
test: headers $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; FUZZ_STRIDE=$(FUZZ_STRIDE) ./$$t; done

# Every header must compile when it is the only thing included.
headers:
	@set -e; for h in $(notdir $(HEADERS)); do \
	  echo "#include \"arrocco/puzzles/$$h\"" | $(CXX) $(CXXFLAGS) -Werror -I$(LIB) -x c++ -fsyntax-only -; \
	done; \
	for h in $(notdir $(MODE_H)); do \
	  echo "#include \"arrocco/ui/$$h\"" | $(CXX) $(CXXFLAGS) -Werror -I$(LIB) -x c++ -fsyntax-only -; \
	done; echo "== headers: $(words $(HEADERS) $(MODE_H)) compile alone"

$(BUILD)/pack: pack_main.cpp $(SRC) $(HEADERS) puzzle_reference.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(LIB) -I. $< $(SRC) -o $@

$(BUILD)/session: session_main.cpp $(SRC) $(MODE) $(HEADERS) $(MODE_H)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -Wshadow -I$(LIB) $< $(SRC) $(MODE) -o $@

# -Ifuzz first so fuzz/arrocco/puzzles/puzzle_data.h shadows the generated header
# and the reader decodes writable, corruptible arrays. The reader is the real one.
$(BUILD)/fuzz: fuzz_main.cpp $(READER) $(CHESS) $(HEADERS) fuzz/arrocco/puzzles/puzzle_data.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -Ifuzz -I$(LIB) -I. $< $(READER) $(CHESS) -o $@

# A second opinion on the same tests.  Out-of-bounds reads are already caught in a
# plain build by the guard pages in fuzz_main, so this is here for the rest of it:
# signed overflow, bad shifts, misaligned loads.
#
# UndefinedBehaviorSanitizer is the default because it is honest and cheap (the
# whole thing still runs in a few seconds).  AddressSanitizer is available with
#   make sanitize SAN=address,undefined SAN_STRIDE=23
# but be warned: on this move generator it costs upwards of a thousand times the
# run time, so thin the byte sweep with SAN_STRIDE or it will not finish.
SAN ?= undefined
SAN_STRIDE ?= 1
sanitize:
	@$(MAKE) --no-print-directory test BUILD=build-san FUZZ_STRIDE=$(SAN_STRIDE) \
	  CXXFLAGS="$(CXXFLAGS) -g -fsanitize=$(SAN) -fno-sanitize-recover=all -fno-omit-frame-pointer"

clean:
	rm -rf $(BUILD) build-san
