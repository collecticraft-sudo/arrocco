# SPDX-License-Identifier: GPL-3.0-or-later
# Native tests for the chess rules library. `make test` builds and runs everything.
CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic
ROOT     := ../..
SRC      := $(wildcard $(ROOT)/lib/arrocco/src/arrocco/chess/*.cpp)
BUILD    := build
TESTS    := $(patsubst %_main.cpp,$(BUILD)/%,$(wildcard *_main.cpp))

HEADERS  := $(wildcard $(ROOT)/lib/arrocco/src/arrocco/chess/*.h)

.PHONY: test headers sanitize clean
test: headers $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$$t; done

# Every header must compile when it is the only thing included.
headers:
	@set -e; for h in $(notdir $(HEADERS)); do \
	  echo "#include \"arrocco/chess/$$h\"" | $(CXX) $(CXXFLAGS) -Werror -I$(ROOT)/lib/arrocco/src -x c++ -fsyntax-only -; \
	done; echo "== headers: $(words $(HEADERS)) compile alone"

$(BUILD)/%: %_main.cpp $(SRC) $(wildcard $(ROOT)/lib/arrocco/src/arrocco/chess/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(ROOT)/lib/arrocco/src $< $(SRC) -o $@

# The same runners under UBSan (not part of `test`: slower). Where AddressSanitizer works, add it:
#   make sanitize SANITIZERS=address,undefined
# (on macOS 26 with the Command Line Tools even an empty ASan program hangs at start-up).
SANITIZERS ?= undefined
sanitize:
	@mkdir -p $(BUILD)/san
	@set -e; for t in $(patsubst %_main.cpp,%,$(wildcard *_main.cpp)); do \
	  $(CXX) -std=c++17 -O1 -g -fno-exceptions -fno-rtti -fsanitize=$(SANITIZERS) -fno-sanitize-recover=undefined \
	    -Wall -Wextra -Wpedantic -I$(ROOT)/lib/arrocco/src $${t}_main.cpp $(SRC) -o $(BUILD)/san/$$t; \
	  echo "== $(BUILD)/san/$$t"; ./$(BUILD)/san/$$t; \
	done

clean:
	rm -rf $(BUILD)
