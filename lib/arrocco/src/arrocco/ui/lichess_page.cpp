// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the Lichess screens' drawing helpers. See lichess_page.h.
#include "arrocco/ui/lichess_page.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <Adafruit_GFX.h>

namespace arrocco::ui {

namespace {

constexpr int kLineMax = 96;

}  // namespace

int formatText(char* out, size_t size, const char* format, ...) {
  if (out == nullptr || size == 0) return 0;
  va_list args;
  va_start(args, format);
  const int n = std::vsnprintf(out, size, format, args);
  va_end(args);
  if (n < 0) out[0] = '\0';
  return n;
}

int16_t drawWrapped(Adafruit_GFX& gfx, Font font, int16_t x, int16_t baseline, int16_t width, int16_t lineHeight,
                    const char* text, int16_t bottom) {
  if (text == nullptr) return baseline;
  char line[kLineMax];
  const char* p = text;
  while (*p != '\0') {
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    if (baseline > bottom) break;
    // Take words while the line still fits; at least one character, so a long word cannot stall.
    int length = 0;        // characters in `line`
    int fitted = 0;        // characters up to the last word that fitted
    const char* q = p;
    while (*q != '\0' && length < kLineMax - 1) {
      const char* wordEnd = q;
      while (*wordEnd != '\0' && *wordEnd != ' ') ++wordEnd;
      int tryLength = length;
      if (tryLength > 0 && tryLength < kLineMax - 1) line[tryLength++] = ' ';
      for (const char* c = q; c < wordEnd && tryLength < kLineMax - 1; ++c) line[tryLength++] = *c;
      line[tryLength] = '\0';
      if (textWidth(gfx, font, line) > width) {
        if (fitted == 0) {
          // One word longer than the line: as many of its characters as fit.
          int cut = tryLength;
          while (cut > 1) {
            line[--cut] = '\0';
            if (textWidth(gfx, font, line) <= width) break;
          }
          fitted = cut;
          q += cut;
        }
        break;
      }
      length = tryLength;
      fitted = length;
      q = wordEnd;
      while (*q == ' ') ++q;
    }
    line[fitted] = '\0';
    drawText(gfx, font, x, baseline, line);
    baseline = static_cast<int16_t>(baseline + lineHeight);
    if (q == p) ++q;   // never stand still
    p = q;
  }
  return baseline;
}

void fitText(Adafruit_GFX& gfx, Font font, const char* text, int16_t width, char* out, int outSize) {
  if (out == nullptr || outSize <= 0) return;
  out[0] = '\0';
  if (text == nullptr) return;
  int n = 0;
  while (text[n] != '\0' && n + 1 < outSize) {
    out[n] = text[n];
    ++n;
  }
  out[n] = '\0';
  if (textWidth(gfx, font, out) <= width) return;
  // Shorten one character at a time, keeping room for the three dots.
  while (n > 0) {
    --n;
    if (n + 4 > outSize) continue;
    out[n] = '.';
    out[n + 1] = '.';
    out[n + 2] = '.';
    out[n + 3] = '\0';
    if (textWidth(gfx, font, out) <= width) return;
  }
  out[0] = '\0';
}

}  // namespace arrocco::ui
