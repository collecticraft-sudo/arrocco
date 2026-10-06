// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the username keyboard. See name_keyboard.h.
#include "arrocco/ui/name_keyboard.h"

#include <Adafruit_GFX.h>

#include "arrocco/platform.h"
#include "arrocco/ui/strings.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui::keyboard {

namespace {

// Row by row; '\b' is the Delete key, which takes the last two columns of the last row.
constexpr char kLayout[kRows][kColumns + 1] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl-",
    "zxcvbnm_\b\b",
};

// One baseline for every key, so that "g", "-" and "_" sit where a reader expects them instead
// of each being centred on its own ink (which put the underscore in the middle, like a dash).
constexpr int16_t kKeyBaseline = 38;   // below the key's top, for Bold18

void drawKey(Adafruit_GFX& gfx, const Rect& r, char ch) {
  drawButton(gfx, r, Font::Bold18, "");
  const char label[2] = {ch, '\0'};
  setFont(gfx, Font::Bold18);
  int16_t bx = 0;
  int16_t by = 0;
  uint16_t bw = 0;
  uint16_t bh = 0;
  gfx.getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
  gfx.setTextColor(kBlack);
  gfx.setCursor(static_cast<int16_t>(r.cx() - static_cast<int16_t>(bw) / 2 - bx), static_cast<int16_t>(r.y + kKeyBaseline));
  gfx.print(label);
}

Rect cell(int row, int column) {
  return Rect{static_cast<int16_t>(kX0 + column * (kKeyW + kGap)), static_cast<int16_t>(kY0 + row * (kKeyH + kGap)),
              kKeyW, kKeyH};
}

}  // namespace

Rect deleteRect() {
  const Rect first = cell(kRows - 1, kColumns - 2);
  return Rect{first.x, first.y, static_cast<int16_t>(2 * kKeyW + kGap), kKeyH};
}

Rect keyRect(char ch) {
  for (int row = 0; row < kRows; ++row)
    for (int column = 0; column < kColumns; ++column)
      if (kLayout[row][column] == ch && ch != '\b') return cell(row, column);
  return Rect{0, 0, 0, 0};
}

Hit hitAt(int16_t x, int16_t y) {
  Hit hit;
  if (kCancel.contains(x, y)) hit.key = Key::Cancel;
  else if (kClear.contains(x, y)) hit.key = Key::Clear;
  else if (kDone.contains(x, y)) hit.key = Key::Done;
  else if (deleteRect().contains(x, y)) hit.key = Key::Delete;
  if (hit.key != Key::None) return hit;
  for (int row = 0; row < kRows; ++row) {
    for (int column = 0; column < kColumns; ++column) {
      const char ch = kLayout[row][column];
      if (ch == '\b' || !cell(row, column).contains(x, y)) continue;
      hit.key = Key::Char;
      hit.ch = ch;
      return hit;
    }
  }
  return hit;   // a gap between keys: nothing
}

void draw(Adafruit_GFX& gfx, const char* title, const char* text, const char* message) {
  drawText(gfx, Font::Bold18, kX0, kTitleBaseline, title);

  // The field: a single frame, the text in Bold18, and a block cursor after it.
  gfx.drawRect(kField.x, kField.y, kField.w, kField.h, kBlack);
  gfx.drawRect(static_cast<int16_t>(kField.x + 1), static_cast<int16_t>(kField.y + 1),
               static_cast<int16_t>(kField.w - 2), static_cast<int16_t>(kField.h - 2), kBlack);
  const int16_t textX = static_cast<int16_t>(kField.x + 16);
  const int16_t baseline = kFieldBaseline;
  drawText(gfx, Font::Bold18, textX, baseline, text);
  const int16_t cursorX = static_cast<int16_t>(textX + (text[0] != '\0' ? textWidth(gfx, Font::Bold18, text) + 4 : 0));
  gfx.fillRect(cursorX, static_cast<int16_t>(baseline - 26), 3, 30, kBlack);
  if (message != nullptr && message[0] != '\0') drawText(gfx, Font::Sans9, kX0, kMessageBaseline, message);

  for (int row = 0; row < kRows; ++row) {
    for (int column = 0; column < kColumns; ++column) {
      const char ch = kLayout[row][column];
      if (ch != '\b') drawKey(gfx, cell(row, column), ch);
    }
  }
  drawButton(gfx, deleteRect(), Font::Bold12, str::kKeyboardDelete);
  drawButton(gfx, kCancel, Font::Bold12, str::kCancel);
  drawButton(gfx, kClear, Font::Bold12, str::kKeyboardClear);
  drawButton(gfx, kDone, Font::Bold12, str::kKeyboardDone);
}

}  // namespace arrocco::ui::keyboard
