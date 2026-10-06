# SPDX-License-Identifier: GPL-3.0-or-later
# Native tests for the firmware's touch replay rules (src/app/touch_policy.h): which taps
# made during a panel refresh still count. `make test` builds and runs them.
CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wshadow
ROOT     := ../..
BUILD    := build
HEADER   := $(ROOT)/src/app/touch_policy.h

.PHONY: test clean
test: $(BUILD)/touch
	@echo "#include \"touch_policy.h\"" | $(CXX) $(CXXFLAGS) -Werror -I$(ROOT)/src/app -x c++ -fsyntax-only -
	@echo "== header compiles alone"
	@./$(BUILD)/touch

$(BUILD)/touch: touch_main.cpp $(HEADER)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(ROOT)/src/app touch_main.cpp -o $@

clean:
	rm -rf $(BUILD)
