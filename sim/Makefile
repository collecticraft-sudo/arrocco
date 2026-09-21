# SPDX-License-Identifier: GPL-3.0-or-later
# Arrocco simulator — native build of arrocco-sim. Run it through sim/build.sh, which
# creates the build/gfx symlink this file relies on. Every path here is relative to
# sim/ on purpose (make breaks on spaces in absolute paths).
#
#   make            build build/arrocco-sim
#   make CORE=0     leave the portable core (lib/arrocco/src/**/*.cpp) out of the build
#   make V=1        show full compiler command lines
#   make clean

CXX      ?= clang++
BUILD    := build
OBJ      := $(BUILD)/obj
GFX      := $(BUILD)/gfx
CORE_DIR := ../lib/arrocco/src
TARGET   := $(BUILD)/arrocco-sim
CORE     ?= 1

# The same dialect restrictions as the firmware, so that code which builds here
# also builds there: C++17, no exceptions, no RTTI.
# ARDUINO must be set on the command line: Adafruit_GFX.h tests it before including
# Arduino.h. No ESP32/ESP8266/__AVR__ define: GFX then takes its generic code path,
# which is the one the ESP32 uses too (plain pointers, no PROGMEM tricks).
BASE_FLAGS := -std=c++17 -O2 -g -fno-exceptions -fno-rtti -DARDUINO=10819 -DARROCCO_SIM=1
INCLUDES   := -Iarduino_shim -Ihost -I$(CORE_DIR) -isystem $(GFX)
# Ours: warnings on, and they must stay at zero.
OUR_WARN   := -Wall -Wextra -Wpedantic -Wshadow
# Third party (Adafruit GFX): not ours to fix, silenced for that one file only.
THIRD_WARN := -w

HOST_SRCS := $(sort $(wildcard host/*.cpp))
SHIM_SRCS := arduino_shim/Print.cpp
ifeq ($(CORE),1)
CORE_SRCS := $(shell find $(CORE_DIR) -name '*.cpp' 2>/dev/null | LC_ALL=C sort)
else
CORE_SRCS :=
endif

HOST_OBJS := $(patsubst host/%.cpp,$(OBJ)/host/%.o,$(HOST_SRCS))
SHIM_OBJS := $(patsubst arduino_shim/%.cpp,$(OBJ)/shim/%.o,$(SHIM_SRCS))
CORE_OBJS := $(patsubst $(CORE_DIR)/%.cpp,$(OBJ)/core/%.o,$(CORE_SRCS))
GFX_OBJS  := $(OBJ)/gfx/Adafruit_GFX.o
OBJS      := $(HOST_OBJS) $(SHIM_OBJS) $(CORE_OBJS) $(GFX_OBJS)

ifeq ($(V),1)
Q :=
else
Q := @
endif

.PHONY: all clean
all: $(TARGET)

ifneq ($(MAKECMDGOALS),clean)
ifeq ($(wildcard $(GFX)/Adafruit_GFX.cpp),)
$(error $(GFX)/Adafruit_GFX.cpp is missing: build with sim/build.sh, it locates the Adafruit GFX Library that PlatformIO downloaded)
endif
endif

$(TARGET): $(OBJS)
	@echo "  LINK  $@"
	$(Q)$(CXX) $(BASE_FLAGS) -o $@ $(OBJS) -lpthread

$(OBJ)/host/%.o: host/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $<"
	$(Q)$(CXX) $(BASE_FLAGS) $(OUR_WARN) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJ)/shim/%.o: arduino_shim/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $<"
	$(Q)$(CXX) $(BASE_FLAGS) $(OUR_WARN) $(INCLUDES) -MMD -MP -c $< -o $@

$(OBJ)/core/%.o: $(CORE_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $<"
	$(Q)$(CXX) $(BASE_FLAGS) $(OUR_WARN) $(INCLUDES) -MMD -MP -c $< -o $@

# glcdfont.c is #included by Adafruit_GFX.cpp, so this one object is the whole library
# as far as the simulator is concerned (canvases, primitives, both font renderers).
$(OBJ)/gfx/Adafruit_GFX.o: $(GFX)/Adafruit_GFX.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $< (third party, warnings silenced)"
	$(Q)$(CXX) $(BASE_FLAGS) $(THIRD_WARN) $(INCLUDES) -MMD -MP -c $< -o $@

clean:
	rm -rf $(OBJ) $(TARGET) $(TARGET).dSYM

-include $(OBJS:.o=.d)
