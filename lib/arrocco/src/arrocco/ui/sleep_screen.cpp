// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the sleep screen. See sleep_screen.h.
#include "arrocco/ui/sleep_screen.h"

#include <Adafruit_GFX.h>

#include "arrocco/platform.h"
#include "arrocco/ui/layout.h"
#include "arrocco/ui/strings.h"
#include "arrocco/ui/text.h"

// The one place the 48 KB picture is included. ARROCCO_NO_SLEEP_ART forces the note even
// when the header exists (the tests draw both).
#if !defined(ARROCCO_NO_SLEEP_ART) && __has_include("arrocco/ui/sleep_art.h")
#include "arrocco/ui/sleep_art.h"
#define ARROCCO_SLEEP_ART 1
#else
#define ARROCCO_SLEEP_ART 0
#endif

namespace arrocco::ui {

bool sleepPictureAvailable() { return ARROCCO_SLEEP_ART != 0; }

SleepScreen drawSleepScreen(Adafruit_GFX& gfx) {
  // Called outside ChessApp::present(), which is what normally sets these: drawCentered()
  // does not, and GFX starts out with white text.
  gfx.setTextWrap(false);
  gfx.setTextSize(1);
  gfx.setTextColor(kBlack);
#if ARROCCO_SLEEP_ART
  static_assert(art::kSleepArtW == arrocco::kScreenW && art::kSleepArtH == arrocco::kScreenH,
                "the sleep picture must cover the whole panel");
  gfx.fillScreen(kWhite);
  gfx.drawBitmap(0, 0, art::kSleepArt, art::kSleepArtW, art::kSleepArtH, kBlack);
  drawBox(gfx, kSleepLabelBox);
  drawCentered(gfx, Font::Bold12, str::kSleepTapToWake, kSleepLabelBox.cx(),
               static_cast<int16_t>(kSleepLabelBox.y + kSleepLabelLine1));
  drawCentered(gfx, Font::Sans9, str::kSleepArtCredit, kSleepLabelBox.cx(),
               static_cast<int16_t>(kSleepLabelBox.y + kSleepLabelLine2));
  return SleepScreen::Picture;
#else
  drawBox(gfx, kSleepNoteBox);
  drawCentered(gfx, Font::Bold12, str::kSleepNote, kSleepNoteBox.cx(), kSleepNoteBox.cy());
  return SleepScreen::Note;
#endif
}

}  // namespace arrocco::ui
