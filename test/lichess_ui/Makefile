# SPDX-License-Identifier: GPL-3.0-or-later
# Native tests for what the Lichess screens add (lib/arrocco/src/arrocco/ui/lichess_*, qr_code,
# name_keyboard): the recent opponents and their blob, usernames, the keyboard's keys, wrapped
# text, the waits as the screens say them, which invitations the board can play, and the QR
# codes drawn module by module into a real 800x480 canvas.
#
# On a Mac the codes are also read back from PNG files by Core Image (test/ui/qr_decode.m), the
# way a phone camera would read them, and must say exactly what was encoded.
#
# It compiles the same Adafruit GFX and QR encoder the simulator does, through the links
# sim/build.sh makes: run sim/build.sh once first. Every path is relative (make breaks on spaces).
CXX      ?= clang++
CC       ?= clang
ROOT     := ../..
GFX      := $(ROOT)/sim/build/gfx
QR       := $(ROOT)/sim/build/qrcodegen
SHIM     := $(ROOT)/sim/arduino_shim
CORE     := $(ROOT)/lib/arrocco/src
BUILD    := build
FLAGS    := -std=c++17 -O2 -fno-exceptions -fno-rtti -DARDUINO=10819 -DARROCCO_SIM=1
INCS     := -I$(SHIM) -I$(CORE) -isystem $(GFX) -isystem $(QR)/src
WARN     := -Wall -Wextra -Wpedantic -Wshadow
UI       := $(addprefix $(CORE)/arrocco/ui/,lichess_friends.cpp lichess_page.cpp lichess_state.cpp \
              name_keyboard.cpp qr_code.cpp text.cpp saved_game.cpp clock.cpp)
LICHESS  := $(wildcard $(CORE)/arrocco/lichess/*.cpp)
CHESS    := $(wildcard $(CORE)/arrocco/chess/*.cpp)
SRC      := screens_main.cpp $(UI) $(LICHESS) $(CHESS) $(SHIM)/Print.cpp
DEPS     := $(SRC) $(wildcard $(CORE)/arrocco/ui/*.h) $(wildcard $(CORE)/arrocco/lichess/*.h)
DECODER  := $(ROOT)/test/ui/qr_decode.m

.PHONY: test headers clean
test: headers $(BUILD)/screens
	@./$(BUILD)/screens $(BUILD)
	@if [ "$$(uname)" = Darwin ] && $(CC) -fobjc-arc -Wall -Wextra -framework Foundation -framework CoreImage \
	     $(DECODER) -o $(BUILD)/qr_decode 2>/dev/null; then \
	  ./$(BUILD)/qr_decode $(BUILD)/qr_*.png > $(BUILD)/qr_read.txt && \
	  diff $(BUILD)/qr_expected.txt $(BUILD)/qr_read.txt && \
	  echo "== the codes read back by Core Image say exactly what was encoded ($$(wc -l < $(BUILD)/qr_expected.txt | tr -d ' '))"; \
	else echo "== qr_decode: not a Mac, the codes are checked module by module only"; fi

# The new headers must compile when they are the only thing included.
headers:
	@set -e; for h in lichess_friends.h lichess_page.h lichess_state.h name_keyboard.h qr_code.h \
	  lichess_screen.h lichess_game_screen.h; do \
	  echo "#include \"arrocco/ui/$$h\"" | $(CXX) $(FLAGS) $(WARN) -Werror $(INCS) -x c++ -fsyntax-only -; \
	done; echo "== headers: the Lichess screens' headers compile alone"

ifneq ($(MAKECMDGOALS),clean)
ifeq ($(wildcard $(GFX)/Adafruit_GFX.cpp),)
$(error $(GFX)/Adafruit_GFX.cpp is missing: run sim/build.sh once, it links the libraries PlatformIO downloaded)
endif
endif

$(BUILD)/gfx.o: $(GFX)/Adafruit_GFX.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(FLAGS) -w $(INCS) -c $< -o $@

$(BUILD)/qrcodegen.o: $(QR)/src/qrcodegen.c
	@mkdir -p $(BUILD)
	$(CC) -std=c99 -O2 -w -c $< -o $@

$(BUILD)/screens: $(DEPS) $(BUILD)/gfx.o $(BUILD)/qrcodegen.o
	$(CXX) $(FLAGS) $(WARN) $(INCS) $(SRC) $(BUILD)/gfx.o $(BUILD)/qrcodegen.o -o $@

clean:
	rm -rf $(BUILD)
