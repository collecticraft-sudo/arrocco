// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the geometry of the Lichess screens and the few drawing helpers they share.
// Everything here is new: layout.h (shared with the other screens) is left as it is.
//
// Two shapes of page:
//   - a menu page: the title and a column of big buttons, menuButtonRect() of layout.h;
//   - a code page (Wi-Fi, login): the QR code where the board would be, in the 448 px square
//     on the left, and text plus buttons in the side column, sideButtonRect() of layout.h.
#pragma once
#include <cstddef>
#include <cstdint>

#include "arrocco/ui/layout.h"
#include "arrocco/ui/text.h"

class Adafruit_GFX;

namespace arrocco::ui {

namespace lichess_page {

// ---- code pages
constexpr Rect kQrBox{kBoardX, kBoardY, kBoardPx, kBoardPx};
constexpr int kQrMaxModulePx = 12;
constexpr int16_t kHeadBaseline = 44;          // Bold18, as the game's headline
constexpr int16_t kTextTop = 76;               // first baseline of the text under the headline
constexpr int16_t kLine12 = 24;                // Sans12 / Bold12 line height
constexpr int16_t kLine9 = 19;                 // Sans9 line height
constexpr int16_t kParagraphGap = 8;
constexpr int16_t kTextBottom = 352;           // the text stops above the second button row

// ---- invitations: one row per challenge, label on the left, Accept and Decline on the right
constexpr int kInviteRows = 4;
constexpr int16_t kInviteY0 = 96;
constexpr int16_t kInviteStep = 70;
constexpr int16_t kInviteLabelX = 32;
constexpr int16_t kInviteButtonW = 152;
constexpr int16_t kInviteAcceptX = 448;
constexpr int16_t kInviteDeclineX = 616;
constexpr Rect inviteAcceptRect(int row) {
  return Rect{kInviteAcceptX, static_cast<int16_t>(kInviteY0 + row * kInviteStep), kInviteButtonW, kMinButtonH};
}
constexpr Rect inviteDeclineRect(int row) {
  return Rect{kInviteDeclineX, static_cast<int16_t>(kInviteY0 + row * kInviteStep), kInviteButtonW, kMinButtonH};
}

// ---- the menu pages' status line, between the subtitle and the first button
constexpr int16_t kStatusBaseline = 80;

}  // namespace lichess_page

// Word-wraps `text` into lines at most `width` px wide, the first baseline at `baseline`, and
// returns the baseline the next line would have. A word wider than the line is cut where it must
// be. Never draws below `bottom` (the rest is simply left out).
int16_t drawWrapped(Adafruit_GFX& gfx, Font font, int16_t x, int16_t baseline, int16_t width, int16_t lineHeight,
                    const char* text, int16_t bottom = arrocco::kScreenH);

// A wait as the screens say it: whole tens of seconds, the nearest one, never 0 while it lasts
// (61 s is "60 s"; a countdown repainted every second would be sixty refreshes).
constexpr int waitTensOfSeconds(uint32_t ms) {
  return ms == 0 ? 0 : (ms + 5000u < 10000u ? 10 : static_cast<int>((ms + 5000u) / 10000u) * 10);
}

// snprintf for the Lichess screens. These texts are cut to their buffers on purpose (names,
// Lichess's error sentences), and GCC's -Wformat-truncation, which the firmware build turns on
// for this library, would flag every such line: the work is done by vsnprintf, which it does not
// second-guess. Always NUL-terminated.
int formatText(char* out, size_t size, const char* format, ...) __attribute__((format(printf, 3, 4)));

// Copies `text` into `out`, shortened with "..." until it fits `width` px in `font`.
void fitText(Adafruit_GFX& gfx, Font font, const char* text, int16_t width, char* out, int outSize);

}  // namespace arrocco::ui
