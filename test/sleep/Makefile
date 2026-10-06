# SPDX-License-Identifier: GPL-3.0-or-later
# The sleep screen (lib/arrocco/src/arrocco/ui/sleep_screen.h), drawn with the real fonts
# into an 800x480 canvas and checked: the picture variant and the note variant (built
# with ARROCCO_NO_SLEEP_ART). `make test` also leaves both screens in build/ as PBM.
#
# It compiles the same Adafruit GFX the simulator does, through the link sim/build.sh
# makes: run sim/build.sh once first. Every path is relative (make breaks on spaces).
CXX      ?= clang++
ROOT     := ../..
GFX      := $(ROOT)/sim/build/gfx
SHIM     := $(ROOT)/sim/arduino_shim
CORE     := $(ROOT)/lib/arrocco/src
BUILD    := build
FLAGS    := -std=c++17 -O2 -fno-exceptions -fno-rtti -DARDUINO=10819 -DARROCCO_SIM=1
INCS     := -I$(SHIM) -I$(CORE) -isystem $(GFX)
WARN     := -Wall -Wextra -Wpedantic -Wshadow
SRC      := sleep_main.cpp $(CORE)/arrocco/ui/sleep_screen.cpp $(CORE)/arrocco/ui/text.cpp $(SHIM)/Print.cpp
DEPS     := $(SRC) $(wildcard $(CORE)/arrocco/ui/*.h) $(CORE)/arrocco/platform.h

.PHONY: test clean
test: $(BUILD)/sleep_picture $(BUILD)/sleep_note
	@./$(BUILD)/sleep_picture $(BUILD)
	@./$(BUILD)/sleep_note $(BUILD)

ifneq ($(MAKECMDGOALS),clean)
ifeq ($(wildcard $(GFX)/Adafruit_GFX.cpp),)
$(error $(GFX)/Adafruit_GFX.cpp is missing: run sim/build.sh once, it links the Adafruit GFX Library PlatformIO downloaded)
endif
endif

$(BUILD)/gfx.o: $(GFX)/Adafruit_GFX.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(FLAGS) -w $(INCS) -c $< -o $@

$(BUILD)/sleep_picture: $(DEPS) $(BUILD)/gfx.o
	$(CXX) $(FLAGS) $(WARN) $(INCS) $(SRC) $(BUILD)/gfx.o -o $@

$(BUILD)/sleep_note: $(DEPS) $(BUILD)/gfx.o
	$(CXX) $(FLAGS) $(WARN) $(INCS) -DARROCCO_NO_SLEEP_ART $(SRC) $(BUILD)/gfx.o -o $@

clean:
	rm -rf $(BUILD)
