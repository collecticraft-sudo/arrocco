// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — text and widget primitives on top of Adafruit_GFX: the FreeSans fonts
// the whole UI uses, centred text, buttons, dialog boxes.
#pragma once
#include <cstdint>

#include "arrocco/ui/layout.h"

class Adafruit_GFX;

namespace arrocco::ui {

enum class Font : uint8_t { Sans9, Sans12, Bold9, Bold12, Bold18, Bold24 };

void setFont(Adafruit_GFX& gfx, Font font);

// Draws `text` with its baseline at (x, baseline).
void drawText(Adafruit_GFX& gfx, Font font, int16_t x, int16_t baseline, const char* text);

// Centres the glyphs' real bounding box on (cx, cy): exact for every letter.
void drawCentered(Adafruit_GFX& gfx, Font font, const char* text, int16_t cx, int16_t cy);

// Width in pixels of `text` in `font`.
int16_t textWidth(Adafruit_GFX& gfx, Font font, const char* text);

// A framed, rounded button. A label too wide for it drops to Bold9 rather than spill over.
// Disabled: a dotted frame and, when given, a small note under the label.
void drawButton(Adafruit_GFX& gfx, const Rect& r, Font font, const char* label, bool enabled = true,
                const char* note = nullptr);

// A white box with a double frame: the background of every popup.
void drawBox(Adafruit_GFX& gfx, const Rect& r);

// A popup: title, optional subtitle, up to kDialogMaxButtons buttons laid out by the caller.
struct Dialog {
  Rect box;
  const char* title = nullptr;
  const char* subtitle = nullptr;
  int buttonCount = 0;
  const char* labels[kDialogMaxButtons] = {};
  Rect buttons[kDialogMaxButtons] = {};
};

void drawDialog(Adafruit_GFX& gfx, const Dialog& d);
int dialogButtonAt(const Dialog& d, int16_t x, int16_t y);  // -1 = none

// The three canonical dialog layouts.
Dialog confirmDialog(const char* resignLabel);                                  // resign / draw / cancel
Dialog gameOverDialog(const char* result, const char* reason);                  // new game / review / menu

}  // namespace arrocco::ui
