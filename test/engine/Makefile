# SPDX-License-Identifier: GPL-3.0-or-later
# Native tests for the CT800 port. `make test` builds the engine and runs them.
#
# The engine is compiled here exactly as PlatformIO and sim/Makefile compile it: C99,
# -fno-strict-aliasing -fno-strict-overflow -fno-common, and ct800_memory.h forced in
# ahead of every translation unit. play.c and tuner.c are NOT built - play.c is the
# desktop UCI front end, which lib/ct800/port/ct800_glue.c replaces.
CXX      ?= clang++
CC       ?= clang
ROOT     := ../..
BUILD    := build
OBJ      := $(BUILD)/obj

CT800    := $(ROOT)/lib/ct800
CT800_UP := $(CT800)/upstream/application-uci
CORE     := $(ROOT)/lib/arrocco/src

CXXFLAGS ?= -std=c++17 -O2 -fno-exceptions -fno-rtti -Wall -Wextra
CFLAGS   := -std=c99 -O2 -fno-strict-aliasing -fno-strict-overflow -fno-common
CINCS    := -I$(CT800_UP) -I$(CT800)/port -include ct800_memory.h

UP_SRCS  := search.c eval.c move_gen.c hashtables.c book.c bookdata.c kpk.c kpk_table.c util.c
UP_OBJS  := $(patsubst %.c,$(OBJ)/up/%.o,$(UP_SRCS))
GLUE_OBJ := $(OBJ)/port/ct800_glue.o
CHESS    := $(wildcard $(CORE)/arrocco/chess/*.cpp)
TARGET   := $(BUILD)/engine
# The seam test drives the simulator's arrocco::Engine (one std::thread around the same
# Searcher): the only implementation of the seam a Mac can actually run.
SEAM     := $(BUILD)/seam
HOSTDIR  := $(ROOT)/sim/host

.PHONY: test clean
test: $(TARGET) $(SEAM)
	./$(TARGET)
	./$(SEAM)

$(TARGET): engine_main.cpp $(CHESS) $(UP_OBJS) $(GLUE_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(CORE) -I$(CT800)/port engine_main.cpp $(CHESS) $(UP_OBJS) $(GLUE_OBJ) \
	  -lpthread -o $@

$(SEAM): seam_main.cpp $(HOSTDIR)/host_engine.cpp $(CHESS) $(UP_OBJS) $(GLUE_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(CORE) -I$(CT800)/port -I$(HOSTDIR) seam_main.cpp \
	  $(HOSTDIR)/host_engine.cpp $(CHESS) $(UP_OBJS) $(GLUE_OBJ) -lpthread -o $@

# Third party: not ours to fix, so its warnings are silenced for these files only.
$(OBJ)/up/%.o: $(CT800_UP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w $(CINCS) -c $< -o $@

$(OBJ)/port/%.o: $(CT800)/port/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wextra $(CINCS) -c $< -o $@

clean:
	rm -rf $(BUILD)
