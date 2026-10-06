// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the on-screen keyboard for a Lichess username: exactly the 38 characters a
// username can have (a-z, 0-9, '-', '_'), lower case only because Lichess ignores case, plus a
// double-width Delete. Four rows of ten keys of 68 x 56 px (13.8 x 11.4 mm at 125 PPI), 8 px
// apart: big enough for a fingertip, and one partial refresh per key.
//
//   1 2 3 4 5 6 7 8 9 0
//   q w e r t y u i o p
//   a s d f g h j k l -
//   z x c v b n m _ [Delete]
//
// Above the keys the name typed so far; below them Cancel, Clear and Done. Pure geometry and
// drawing: the screen that owns the text decides what a key does to it.
#pragma once
#include <cstdint>

#include "arrocco/ui/layout.h"

class Adafruit_GFX;

namespace arrocco::ui {

namespace keyboard {

constexpr int kRows = 4;
constexpr int kColumns = 10;
constexpr int16_t kKeyW = 68;
constexpr int16_t kKeyH = 56;
constexpr int16_t kGap = 8;
constexpr int16_t kX0 = (arrocco::kScreenW - (kColumns * kKeyW + (kColumns - 1) * kGap)) / 2;  // 24
constexpr int16_t kY0 = 128;
constexpr Rect kField{kX0, 48, kColumns * kKeyW + (kColumns - 1) * kGap, 48};
constexpr int16_t kTitleBaseline = 36;
constexpr int16_t kFieldBaseline = 82;      // the name typed so far, in Bold18
constexpr int16_t kMessageBaseline = 117;   // a line under the field ("at least 2 letters")
constexpr int16_t kBottomY = 400;
constexpr int16_t kBottomW = 220;
constexpr Rect kCancel{kX0, kBottomY, kBottomW, kMinButtonH};
constexpr Rect kClear{(arrocco::kScreenW - kBottomW) / 2, kBottomY, kBottomW, kMinButtonH};
constexpr Rect kDone{static_cast<int16_t>(arrocco::kScreenW - kX0 - kBottomW), kBottomY, kBottomW, kMinButtonH};

// What a tap on the keyboard page means.
enum class Key : uint8_t { None, Char, Delete, Cancel, Clear, Done };
struct Hit {
  Key key = Key::None;
  char ch = 0;   // for Key::Char
};

Hit hitAt(int16_t x, int16_t y);
// Where the key for `ch` is drawn (for the tests and for the session scripts); w = 0 if none.
Rect keyRect(char ch);
Rect deleteRect();

// The whole page: title, the field with `text` and a cursor, the keys, the three buttons, and
// `message` (or nothing) under the field.
void draw(Adafruit_GFX& gfx, const char* title, const char* text, const char* message);

}  // namespace keyboard

}  // namespace arrocco::ui
