// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — text and widget primitives. See text.h.
#include "arrocco/ui/text.h"

#include <cstring>

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include "arrocco/ui/strings.h"

namespace arrocco::ui {

namespace {

constexpr int16_t kButtonRadius = 8;
constexpr int16_t kDialogTitleOffset = 36;     // title baseline below the box top
constexpr int16_t kDialogSubtitleOffset = 66;
constexpr int16_t kButtonLabelMargin = 8;      // label must leave this much inside the frame

constexpr int16_t kNoteDotGap = 13;            // ink to ink around a kNoteDot; the dot sits in the middle
constexpr int16_t kNoteDotSize = 3;            // a square of 3 x 3 px: a period is about 2 x 2

Font fittingFont(Adafruit_GFX& gfx, const Rect& r, Font font, const char* label) {
  if (textWidth(gfx, font, label) <= r.w - kButtonLabelMargin) return font;
  return font == Font::Bold12 ? Font::Bold9 : Font::Sans9;
}

// The ink box of `text` in the current font, for a cursor at (0, 0) on the baseline.
struct Bounds {
  int16_t x = 0;
  int16_t y = 0;      // negative: the ink starts above the baseline
  int16_t w = 0;
  int16_t h = 0;
};

Bounds boundsOf(Adafruit_GFX& gfx, const char* text) {
  Bounds b;
  uint16_t w = 0;
  uint16_t h = 0;
  gfx.getTextBounds(text, 0, 0, &b.x, &b.y, &w, &h);
  b.w = static_cast<int16_t>(w);
  b.h = static_cast<int16_t>(h);
  return b;
}

// A button's note in Sans9, centred on (cx, cy) as drawCentered() centres text. The
// first kNoteDot splits it into two runs with a dot between them, level with the middle
// of a lower-case letter.
void drawNote(Adafruit_GFX& gfx, const char* note, int16_t cx, int16_t cy) {
  const char* dot = strchr(note, kNoteDot);
  if (dot == nullptr) {
    drawCentered(gfx, Font::Sans9, note, cx, cy);
    return;
  }
  char left[48];
  size_t leftLength = static_cast<size_t>(dot - note);
  if (leftLength > sizeof left - 1) leftLength = sizeof left - 1;
  memcpy(left, note, leftLength);
  left[leftLength] = '\0';
  const char* right = dot + 1;

  setFont(gfx, Font::Sans9);
  // One baseline for both runs, from the bounds of the whole note: the marker byte has no
  // glyph, so those are the bounds of the words alone.
  const Bounds all = boundsOf(gfx, note);
  const int16_t baseline = static_cast<int16_t>(cy - all.h / 2 - all.y);
  const Bounds l = boundsOf(gfx, left);
  const Bounds r = boundsOf(gfx, right);
  const int16_t inkX = static_cast<int16_t>(cx - (l.w + kNoteDotGap + r.w) / 2);
  gfx.setCursor(static_cast<int16_t>(inkX - l.x), baseline);
  gfx.print(left);
  gfx.setCursor(static_cast<int16_t>(inkX + l.w + kNoteDotGap - r.x), baseline);
  gfx.print(right);

  const Bounds x = boundsOf(gfx, "x");
  const int16_t dotX = static_cast<int16_t>(inkX + l.w + (kNoteDotGap - kNoteDotSize) / 2);
  const int16_t dotY = static_cast<int16_t>(baseline + x.y + (x.h - kNoteDotSize) / 2);
  gfx.fillRect(dotX, dotY, kNoteDotSize, kNoteDotSize, kBlack);
}

}  // namespace

void setFont(Adafruit_GFX& gfx, Font font) {
  switch (font) {
    case Font::Sans9:  gfx.setFont(&FreeSans9pt7b); break;
    case Font::Sans12: gfx.setFont(&FreeSans12pt7b); break;
    case Font::Bold9:  gfx.setFont(&FreeSansBold9pt7b); break;
    case Font::Bold12: gfx.setFont(&FreeSansBold12pt7b); break;
    case Font::Bold18: gfx.setFont(&FreeSansBold18pt7b); break;
    case Font::Bold24: gfx.setFont(&FreeSansBold24pt7b); break;
  }
  gfx.setTextSize(1);
  gfx.setTextWrap(false);
}

void drawText(Adafruit_GFX& gfx, Font font, int16_t x, int16_t baseline, const char* text) {
  setFont(gfx, font);
  gfx.setTextColor(kBlack);
  gfx.setCursor(x, baseline);
  gfx.print(text);
}

void drawCentered(Adafruit_GFX& gfx, Font font, const char* text, int16_t cx, int16_t cy) {
  setFont(gfx, font);
  int16_t bx = 0;
  int16_t by = 0;
  uint16_t bw = 0;
  uint16_t bh = 0;
  gfx.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  gfx.setCursor(static_cast<int16_t>(cx - static_cast<int16_t>(bw) / 2 - bx),
                static_cast<int16_t>(cy - static_cast<int16_t>(bh) / 2 - by));
  gfx.print(text);
}

int16_t textWidth(Adafruit_GFX& gfx, Font font, const char* text) {
  setFont(gfx, font);
  int16_t bx = 0;
  int16_t by = 0;
  uint16_t bw = 0;
  uint16_t bh = 0;
  gfx.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  return static_cast<int16_t>(bw);
}

void drawButton(Adafruit_GFX& gfx, const Rect& r, Font requested, const char* label, bool enabled,
                const char* note) {
  gfx.setTextColor(kBlack);
  const Font font = fittingFont(gfx, r, requested, label);
  if (enabled) {
    gfx.drawRoundRect(r.x, r.y, r.w, r.h, kButtonRadius, kBlack);
    gfx.drawRoundRect(static_cast<int16_t>(r.x + 1), static_cast<int16_t>(r.y + 1),
                      static_cast<int16_t>(r.w - 2), static_cast<int16_t>(r.h - 2),
                      static_cast<int16_t>(kButtonRadius - 1), kBlack);
  } else {
    // Disabled: a dotted single frame.
    for (int16_t i = 0; i < r.w; i += 3) {
      gfx.drawPixel(static_cast<int16_t>(r.x + i), r.y, kBlack);
      gfx.drawPixel(static_cast<int16_t>(r.x + i), static_cast<int16_t>(r.y + r.h - 1), kBlack);
    }
    for (int16_t i = 0; i < r.h; i += 3) {
      gfx.drawPixel(r.x, static_cast<int16_t>(r.y + i), kBlack);
      gfx.drawPixel(static_cast<int16_t>(r.x + r.w - 1), static_cast<int16_t>(r.y + i), kBlack);
    }
  }
  if (note == nullptr) {
    drawCentered(gfx, font, label, r.cx(), r.cy());
  } else {
    // The label pushed up to leave room for the note.
    drawCentered(gfx, font, label, r.cx(), static_cast<int16_t>(r.cy() - 8));
    drawNote(gfx, note, r.cx(), static_cast<int16_t>(r.cy() + 12));
  }
}

void drawBox(Adafruit_GFX& gfx, const Rect& r) {
  gfx.fillRect(r.x, r.y, r.w, r.h, kWhite);
  gfx.drawRect(r.x, r.y, r.w, r.h, kBlack);
  gfx.drawRect(static_cast<int16_t>(r.x + 1), static_cast<int16_t>(r.y + 1),
               static_cast<int16_t>(r.w - 2), static_cast<int16_t>(r.h - 2), kBlack);
  gfx.drawRect(static_cast<int16_t>(r.x + 4), static_cast<int16_t>(r.y + 4),
               static_cast<int16_t>(r.w - 8), static_cast<int16_t>(r.h - 8), kBlack);
}

void drawDialog(Adafruit_GFX& gfx, const Dialog& d) {
  drawBox(gfx, d.box);
  if (d.title != nullptr)
    drawCentered(gfx, Font::Bold18, d.title, d.box.cx(),
                 static_cast<int16_t>(d.box.y + kDialogTitleOffset));
  if (d.subtitle != nullptr)
    drawCentered(gfx, Font::Sans12, d.subtitle, d.box.cx(),
                 static_cast<int16_t>(d.box.y + kDialogSubtitleOffset));
  for (int i = 0; i < d.buttonCount; ++i) drawButton(gfx, d.buttons[i], Font::Bold12, d.labels[i]);
}

int dialogButtonAt(const Dialog& d, int16_t x, int16_t y) {
  for (int i = 0; i < d.buttonCount; ++i)
    if (d.buttons[i].contains(x, y)) return i;
  return -1;
}

// Resign / draw: stacked full-width buttons under the question, the box as tall as they
// need. Against the engine there is nobody to agree a draw with: resign and cancel only.
Dialog confirmDialog(const char* resignLabel, bool offerDraw) {
  Dialog d;
  d.box = kConfirmBox;
  d.title = str::kEndGameTitle;
  d.labels[d.buttonCount++] = resignLabel;
  if (offerDraw) d.labels[d.buttonCount++] = str::kAgreeDraw;
  d.labels[d.buttonCount++] = str::kCancel;
  d.box.h = static_cast<int16_t>(kConfirmButtonY0 + d.buttonCount * (kMinButtonH + kButtonGap) + 8);
  const int16_t x = static_cast<int16_t>(d.box.x + (d.box.w - kConfirmButtonW) / 2);
  for (int i = 0; i < d.buttonCount; ++i)
    d.buttons[i] = Rect{x, static_cast<int16_t>(d.box.y + kConfirmButtonY0 + i * (kMinButtonH + kButtonGap)),
                        kConfirmButtonW, kMinButtonH};
  return d;
}

// Before an unfinished game is thrown away: the question, what it costs, and a row of
// two buttons, the box centred on `cx` (the board's centre, or the screen's).
Dialog newGameDialog(int16_t cx) {
  Dialog d;
  d.box = Rect{static_cast<int16_t>(cx - kNewGameBoxW / 2), kNewGameBoxY, kNewGameBoxW, kNewGameBoxH};
  d.title = str::kNewGameTitle;
  d.subtitle = str::kNewGameLost;
  d.buttonCount = 2;
  d.labels[0] = str::kButtonNewGame;
  d.labels[1] = str::kCancel;
  constexpr int16_t kW = 156;
  const int16_t x0 = static_cast<int16_t>(d.box.x + (d.box.w - (2 * kW + kButtonGap)) / 2);
  const int16_t y = static_cast<int16_t>(d.box.y + d.box.h - kMinButtonH - 24);
  for (int i = 0; i < 2; ++i)
    d.buttons[i] = Rect{static_cast<int16_t>(x0 + i * (kW + kButtonGap)), y, kW, kMinButtonH};
  return d;
}

// Game over: result, reason, and a row of three buttons.
Dialog gameOverDialog(const char* result, const char* reason) {
  Dialog d;
  d.box = kGameOverBox;
  d.title = result;
  d.subtitle = reason;
  d.buttonCount = 3;
  d.labels[0] = str::kButtonNewGame;
  d.labels[1] = str::kGameOverReview;
  d.labels[2] = str::kButtonMenu;
  constexpr int16_t kW = 124;
  constexpr int16_t kH = kMinButtonH;
  constexpr int16_t kGap = kButtonGap;
  const int16_t x0 = static_cast<int16_t>(d.box.x + (d.box.w - (3 * kW + 2 * kGap)) / 2);
  const int16_t y = static_cast<int16_t>(d.box.y + d.box.h - kH - 24);
  for (int i = 0; i < 3; ++i)
    d.buttons[i] = Rect{static_cast<int16_t>(x0 + i * (kW + kGap)), y, kW, kH};
  return d;
}

}  // namespace arrocco::ui
