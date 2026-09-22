// SPDX-License-Identifier: GPL-3.0-or-later
// The four test screens and the permanent diagnostics column. Drawing only:
// nothing here refreshes the panel, main.cpp does that once per event.
#pragma once
#include <Arduino.h>

namespace screens {

enum class Id : uint8_t { Board = 0, Touch = 1, Panel = 2, Sound = 3 };

// What main.cpp must do besides the usual single partial refresh.
enum class Action : uint8_t { None, FullRefresh, Beep, Scale, Sweep };

struct TouchPoint {
  bool valid;
  uint16_t rawX;
  uint16_t rawY;
  int16_t x; // mapped to screen pixels
  int16_t y;
};

void begin();
void show(Id id);
Id current();
const char* name(Id id);

// Writes "e4" (or "--" outside the board) into out[3]; true when on the board.
bool squareAt(int16_t x, int16_t y, char out[3]);

// Hit test for a touch-down. The caller logs the touch BEFORE calling this.
Action onTouchDown(const TouchPoint& touch);

// Draws the whole scene into the frame buffer.
void draw(const TouchPoint& lastTouch);

} // namespace screens
