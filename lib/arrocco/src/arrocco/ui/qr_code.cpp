// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — QR codes. See qr_code.h.
#include "arrocco/ui/qr_code.h"

#include <Adafruit_GFX.h>
#include <qrcodegen.h>

#include "arrocco/platform.h"

namespace arrocco::ui {

static_assert(QrCode::kBufferSize == qrcodegen_BUFFER_LEN_FOR_VERSION(QrCode::kMaxVersion),
              "the buffers must be what the encoder asks for");

bool QrCode::encode(const char* text) {
  size_ = 0;
  if (text == nullptr || text[0] == '\0') return false;
  // Byte mode for anything that is not all digits or upper case: the encoder picks the mode.
  if (!qrcodegen_encodeText(text, temp_, code_, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, kMaxVersion,
                            qrcodegen_Mask_AUTO, true))
    return false;
  size_ = qrcodegen_getSize(code_);
  return size_ > 0;
}

bool QrCode::module(int x, int y) const {
  if (size_ <= 0 || x < 0 || y < 0 || x >= size_ || y >= size_) return false;
  return qrcodegen_getModule(code_, x, y);
}

int qrModulePixels(const QrCode& qr, int side, int maxModulePx) {
  if (qr.size() <= 0) return 0;
  const int modules = qr.size() + 2 * QrCode::kQuietModules;
  int px = side / modules;
  if (px > maxModulePx) px = maxModulePx;
  return px > 0 ? px : 0;
}

int drawQr(Adafruit_GFX& gfx, const QrCode& qr, const Rect& box, int maxModulePx) {
  const int side = box.w < box.h ? box.w : box.h;
  const int px = qrModulePixels(qr, side, maxModulePx);
  if (px == 0) return 0;
  const int n = qr.size();
  const int codePx = n * px;
  const int quietPx = QrCode::kQuietModules * px;
  const int x0 = box.x + (box.w - codePx) / 2;
  const int y0 = box.y + (box.h - codePx) / 2;
  gfx.fillRect(static_cast<int16_t>(x0 - quietPx), static_cast<int16_t>(y0 - quietPx),
               static_cast<int16_t>(codePx + 2 * quietPx), static_cast<int16_t>(codePx + 2 * quietPx), kWhite);
  // Runs of dark modules in a row become one rectangle: fewer calls, the same pixels.
  for (int y = 0; y < n; ++y) {
    int x = 0;
    while (x < n) {
      if (!qr.module(x, y)) {
        ++x;
        continue;
      }
      int run = 1;
      while (x + run < n && qr.module(x + run, y)) ++run;
      gfx.fillRect(static_cast<int16_t>(x0 + x * px), static_cast<int16_t>(y0 + y * px),
                   static_cast<int16_t>(run * px), static_cast<int16_t>(px), kBlack);
      x += run;
    }
  }
  return px;
}

}  // namespace arrocco::ui
