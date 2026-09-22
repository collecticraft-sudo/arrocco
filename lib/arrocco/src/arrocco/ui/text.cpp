// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — text and widget primitives. See text.h.
#include "arrocco/ui/text.h"

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

Font fittingFont(Adafruit_GFX& gfx, const Rect& r, Font font, const char* label) {
  if (textWidth(gfx, font, label) <= r.w - kButtonLabelMargin) return font;
  return font == Font::Bold12 ? Font::Bold9 : Font::Sans9;
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
    drawCentered(gfx, font, label, r.cx(), r.cy());
    return;
  }
  // Disabled: a dotted single frame, the label pushed up to leave room for the note.
  for (int16_t i = 0; i < r.w; i += 3) {
    gfx.drawPixel(static_cast<int16_t>(r.x + i), r.y, kBlack);
    gfx.drawPixel(static_cast<int16_t>(r.x + i), static_cast<int16_t>(r.y + r.h - 1), kBlack);
  }
  for (int16_t i = 0; i < r.h; i += 3) {
    gfx.drawPixel(r.x, static_cast<int16_t>(r.y + i), kBlack);
    gfx.drawPixel(static_cast<int16_t>(r.x + r.w - 1), static_cast<int16_t>(r.y + i), kBlack);
  }
  if (note == nullptr) {
    drawCentered(gfx, font, label, r.cx(), r.cy());
  } else {
    drawCentered(gfx, font, label, r.cx(), static_cast<int16_t>(r.cy() - 8));
    drawCentered(gfx, Font::Sans9, note, r.cx(), static_cast<int16_t>(r.cy() + 12));
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

// Resign / draw: three stacked full-width buttons under the question.
Dialog confirmDialog(const char* resignLabel) {
  Dialog d;
  d.box = kConfirmBox;
  d.title = str::kEndGameTitle;
  d.buttonCount = 3;
  d.labels[0] = resignLabel;
  d.labels[1] = str::kAgreeDraw;
  d.labels[2] = str::kCancel;
  const int16_t x = static_cast<int16_t>(d.box.x + (d.box.w - kConfirmButtonW) / 2);
  for (int i = 0; i < 3; ++i)
    d.buttons[i] = Rect{x, static_cast<int16_t>(d.box.y + kConfirmButtonY0 + i * (kMinButtonH + kButtonGap)),
                        kConfirmButtonW, kMinButtonH};
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
