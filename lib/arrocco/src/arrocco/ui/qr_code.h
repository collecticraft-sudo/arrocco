// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — QR codes on the panel: the Wi-Fi network to join and the Lichess login.
//
// The encoder is Project Nayuki's QR Code generator (MIT, THIRD-PARTY.md), which PlatformIO
// fetches as wjtje/qr-code-generator-library; this file only owns the buffers (no heap) and
// draws the modules as whole-pixel squares, so every module edge is sharp on a 1-bit panel.
//
// Sizes, for a phone held at reading distance from a 125 PPI panel: one module of 3 px is 0.6 mm,
// about the smallest a phone camera still reads off e-paper; the Lichess screens get 8 to 12 px
// (1.6 to 2.4 mm) because what they encode is short (see lichess_screen.cpp).
#pragma once
#include <cstdint>

#include "arrocco/ui/layout.h"

class Adafruit_GFX;

namespace arrocco::ui {

class QrCode {
 public:
  // Version 12 is 65 x 65 modules and holds 287 bytes at medium error correction: a whole
  // lichess.org authorize URL fits, though the screens never need that much.
  static constexpr int kMaxVersion = 12;
  static constexpr int kBufferSize = ((kMaxVersion * 4 + 17) * (kMaxVersion * 4 + 17) + 7) / 8 + 1;
  // The white margin a reader needs around the code, in modules (the standard asks for four).
  static constexpr int kQuietModules = 4;

  // Encodes `text` (bytes, as they are) with at least medium error correction, in the smallest
  // version that holds it; higher correction when it fits in the same size. False when it does not
  // fit in kMaxVersion: size() is 0 then, and nothing is drawn.
  bool encode(const char* text);
  void clear() { size_ = 0; }
  int size() const { return size_; }        // modules per side, 0 = nothing encoded
  bool module(int x, int y) const;          // true = dark; false outside the code

 private:
  uint8_t code_[kBufferSize] = {};
  uint8_t temp_[kBufferSize] = {};
  int size_ = 0;
};

// Pixels per module that fit `qr`, quiet zone included, in a `side` x `side` square (0 if none).
int qrModulePixels(const QrCode& qr, int side, int maxModulePx);

// Draws `qr` centred in `box` with the largest whole number of pixels per module (at most
// `maxModulePx`) that keeps the quiet zone inside the box, and paints that zone white. Returns the
// pixels per module, or 0 when nothing was drawn (no code, or it would not fit at 1 px).
int drawQr(Adafruit_GFX& gfx, const QrCode& qr, const Rect& box, int maxModulePx = 12);

}  // namespace arrocco::ui
