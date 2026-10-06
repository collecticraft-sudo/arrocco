// SPDX-License-Identifier: GPL-3.0-or-later
// The sleep screen drawn into a real GFXcanvas1 and checked pixel by pixel:
//  - picture build: the picture is exactly the bitmap everywhere outside the label box,
//    and the label's two lines sit inside the box without touching its frame;
//  - note build (ARROCCO_NO_SLEEP_ART): the screen that was there is untouched outside
//    the note box, and the note fits inside it.
// argv[1] is a directory: the screen is also written there as a PBM, to look at.
#include <Adafruit_GFX.h>

#include <cstdio>
#include <cstring>

#include "arrocco/platform.h"
#include "arrocco/ui/layout.h"
#include "arrocco/ui/sleep_screen.h"
#if !defined(ARROCCO_NO_SLEEP_ART)
#include "arrocco/ui/sleep_art.h"
#endif

namespace {

using arrocco::kScreenH;
using arrocco::kScreenW;
using arrocco::ui::Rect;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL %s\n", what);
  }
}

// GFXcanvas1: a set bit is white (kWhite), a clear bit is ink.
bool ink(GFXcanvas1& c, int x, int y) { return !c.getPixel(static_cast<int16_t>(x), static_cast<int16_t>(y)); }

// What a screen "was" before the sleep: a recognisable pattern.
bool pattern(int x, int y) { return ((x / 8) + (y / 8)) % 3 == 0; }

void fillPattern(GFXcanvas1& c) {
  for (int y = 0; y < kScreenH; ++y)
    for (int x = 0; x < kScreenW; ++x)
      c.drawPixel(static_cast<int16_t>(x), static_cast<int16_t>(y), pattern(x, y) ? arrocco::kBlack : arrocco::kWhite);
}

bool inside(const Rect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

// The box drawBox() makes: frames at 0, 1 and 4 px from the edge, white between them and
// inside. The text must stay at least `margin` px clear of the innermost frame.
void checkBox(GFXcanvas1& c, const Rect& r, int margin, int lines, const char* name) {
  char what[128];
  bool frames = true;
  for (int x = r.x; x < r.x + r.w; ++x)
    frames = frames && ink(c, x, r.y) && ink(c, x, r.y + r.h - 1) && ink(c, x, r.y + 1) && ink(c, x, r.y + r.h - 2);
  for (int y = r.y; y < r.y + r.h; ++y)
    frames = frames && ink(c, r.x, y) && ink(c, r.x + r.w - 1, y) && ink(c, r.x + 1, y) && ink(c, r.x + r.w - 2, y);
  std::snprintf(what, sizeof what, "%s: the double frame is drawn", name);
  check(frames, what);

  // Text pixels: everything inked strictly inside the innermost frame (offset 4).
  const int x0 = r.x + 5, y0 = r.y + 5, x1 = r.x + r.w - 6, y1 = r.y + r.h - 6;
  int minX = kScreenW, minY = kScreenH, maxX = -1, maxY = -1;
  int groups = 0;
  bool previousRowInked = false;
  for (int y = y0; y <= y1; ++y) {
    bool rowInked = false;
    for (int x = x0; x <= x1; ++x) {
      if (!ink(c, x, y)) continue;
      rowInked = true;
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
    }
    if (rowInked && !previousRowInked) ++groups;
    previousRowInked = rowInked;
  }
  std::snprintf(what, sizeof what, "%s: text present (%d line(s) of ink, want %d)", name, groups, lines);
  check(maxX >= 0 && groups == lines, what);
  std::snprintf(what, sizeof what, "%s: text %d..%d x %d..%d clears the frame by %d px", name, minX, maxX, minY, maxY,
                margin);
  check(minX - x0 >= margin - 1 && x1 - maxX >= margin - 1 && minY - y0 >= margin - 1 && y1 - maxY >= margin - 1, what);
  std::printf("  %s: box %dx%d at (%d,%d), text %d px wide, %d px tall, %d px left free on the tighter side\n", name,
              r.w, r.h, r.x, r.y, maxX - minX + 1, maxY - minY + 1,
              (minX - x0) < (x1 - maxX) ? (minX - x0) : (x1 - maxX));
}

void writePbm(GFXcanvas1& c, const char* dir, const char* name) {
  char path[512];
  std::snprintf(path, sizeof path, "%s/%s.pbm", dir, name);
  FILE* f = std::fopen(path, "wb");
  if (!f) {
    check(false, "the PBM can be written");
    return;
  }
  std::fprintf(f, "P4\n%d %d\n", kScreenW, kScreenH);
  for (int y = 0; y < kScreenH; ++y) {
    for (int x = 0; x < kScreenW; x += 8) {
      unsigned char byte = 0;
      for (int b = 0; b < 8; ++b)
        if (ink(c, x + b, y)) byte = static_cast<unsigned char>(byte | (0x80 >> b));
      std::fputc(byte, f);
    }
  }
  std::fclose(f);
  std::printf("  wrote %s\n", path);
}

}  // namespace

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  GFXcanvas1 canvas(kScreenW, kScreenH);
  fillPattern(canvas);
  const arrocco::ui::SleepScreen kind = arrocco::ui::drawSleepScreen(canvas);

#if !defined(ARROCCO_NO_SLEEP_ART)
  std::printf("== sleep screen: picture\n");
  check(arrocco::ui::sleepPictureAvailable(), "the picture is in this build");
  check(kind == arrocco::ui::SleepScreen::Picture, "a picture build draws the picture (and wants a Full refresh)");
  const Rect& box = arrocco::ui::kSleepLabelBox;
  long wrong = 0;
  for (int y = 0; y < kScreenH; ++y) {
    for (int x = 0; x < kScreenW; ++x) {
      if (inside(box, x, y)) continue;
      const int i = y * kScreenW + x;
      const bool bit = (arrocco::ui::art::kSleepArt[i >> 3] & (0x80 >> (i & 7))) != 0;
      if (bit != ink(canvas, x, y)) ++wrong;
    }
  }
  char what[96];
  std::snprintf(what, sizeof what, "outside the label the screen is the picture (%ld pixels differ)", wrong);
  check(wrong == 0, what);
  check(box.x + box.w <= kScreenW - 16 && box.y + box.h <= kScreenH - 16, "the label keeps a 16 px margin");
  checkBox(canvas, box, 6, 2, "label");
  writePbm(canvas, dir, "sleep_picture");
#else
  std::printf("== sleep screen: note (no picture in the build)\n");
  check(!arrocco::ui::sleepPictureAvailable(), "no picture in this build");
  check(kind == arrocco::ui::SleepScreen::Note, "without a picture the note is drawn (a Partial refresh)");
  const Rect& box = arrocco::ui::kSleepNoteBox;
  long changed = 0;
  for (int y = 0; y < kScreenH; ++y)
    for (int x = 0; x < kScreenW; ++x)
      if (!inside(box, x, y) && ink(canvas, x, y) != pattern(x, y)) ++changed;
  char what[96];
  std::snprintf(what, sizeof what, "the screen outside the note is untouched (%ld pixels changed)", changed);
  check(changed == 0, what);
  check(box.x >= arrocco::ui::kSideX, "the note stays in the side column");
  checkBox(canvas, box, 6, 1, "note");
  writePbm(canvas, dir, "sleep_note");
#endif

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
