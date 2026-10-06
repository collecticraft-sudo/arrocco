// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — what the panel shows while the board is in deep sleep. E-paper keeps its
// image with no power, so this is drawn once, just before the firmware goes to sleep, and
// stays until a touch wakes the board (which then reboots and draws its first screen).
//
// The picture is an optional 800x480 1-bit header, arrocco/ui/sleep_art.h:
//   namespace arrocco::ui::art { kSleepArtW = 800; kSleepArtH = 480; kSleepArt[48000]; }
// row-major, MSB first, bit set = ink. A build without that header (or compiled with
// ARROCCO_NO_SLEEP_ART) falls back to a short note drawn over the current screen.
// sleep_screen.cpp is the one translation unit that includes the picture.
#pragma once
#include <cstdint>

class Adafruit_GFX;

namespace arrocco::ui {

enum class SleepScreen : uint8_t {
  Picture,  // the whole buffer was redrawn: push it with a Full refresh
  Note,     // a note was drawn over what the buffer held: a Partial refresh shows it
};

bool sleepPictureAvailable();

// Draws into the frame buffer and says how to push it. Picture: the sleep picture with a
// white "Tap to wake" label (and the picture's credit) in the bottom-right corner. Note:
// "Asleep. Tap to wake." in a framed box over the top of the side column, the rest of the
// screen left as it was, so the board still shows the game it fell asleep on.
SleepScreen drawSleepScreen(Adafruit_GFX& gfx);

}  // namespace arrocco::ui
