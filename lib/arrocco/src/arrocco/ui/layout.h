// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — every pixel constant of the 800x480 landscape layout (docs/decisioni.md)
// and the hit-testing helpers. Everything sits on multiples of 8 where it can.
#pragma once
#include <cstdint>

#include "arrocco/platform.h"

namespace arrocco::ui {

struct Rect {
  int16_t x, y, w, h;
  constexpr bool contains(int16_t px, int16_t py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
  constexpr int16_t cx() const { return static_cast<int16_t>(x + w / 2); }
  constexpr int16_t cy() const { return static_cast<int16_t>(y + h / 2); }
};

// ---- board ------------------------------------------------------------------------
constexpr int16_t kSquare = 56;
constexpr int16_t kBoardX = 16;
constexpr int16_t kBoardY = 16;
constexpr int16_t kBoardPx = 8 * kSquare;  // 448
constexpr Rect kBoardRect{kBoardX, kBoardY, kBoardPx, kBoardPx};
constexpr int16_t kGutter = 16;            // rank letters on the left, file letters below

// Markers, all inside one square.
constexpr int16_t kSelectFrame = 3;        // selected piece: 3 px frame
constexpr int16_t kTargetDotRadius = 7;    // legal target on an empty square
constexpr int16_t kTargetRingRadius = 26;  // legal capture: ring around the piece
constexpr int16_t kTargetRingWidth = 3;
constexpr int16_t kLastMoveCorner = 8;     // corner triangles on from/to squares
constexpr int16_t kCheckRingRadius = 27;   // king in check: ring just outside the halo
constexpr int16_t kCheckRingWidth = 3;
// Both rings stand on a white disc: a black ring on a hatched square would vanish.

// ---- side column (x = 480..799) ----------------------------------------------------
constexpr int16_t kSideX = 480;
constexpr int16_t kSideInnerX = kSideX + 16;                            // 496
constexpr int16_t kSideInnerW = arrocco::kScreenW - 16 - kSideInnerX;  // 288
constexpr int16_t kTurnBaseline = 44;
constexpr int16_t kLastMoveBaseline = 76;
constexpr int16_t kDivider1Y = 88;
constexpr int16_t kClockLabelBaseline = 110;
constexpr int16_t kClockBaseline = 148;
constexpr int16_t kClockColumn2X = 640;
constexpr int16_t kDivider2Y = 164;
constexpr int16_t kMoveListBaseline0 = 184;
constexpr int16_t kMoveListRowH = 20;
constexpr int16_t kMoveListRows = 5;      // x 2 columns = the last 10 moves
constexpr int16_t kMoveListColumn2X = 640;
constexpr int16_t kMaterialBaseline = 288;

// Game buttons: a grid of 3 rows x 2 columns, slot = row * 2 + column. Every button is
// kMinButtonH tall with kButtonGap between rows: a fingertip on glass, not a mouse.
constexpr int16_t kMinButtonH = 48;
constexpr int16_t kButtonGap = 8;
constexpr int kSideButtonSlots = 6;
constexpr int16_t kSideButtonW = 140;
constexpr int16_t kSideButtonH = kMinButtonH;
constexpr int16_t kSideButtonStep = kSideButtonH + kButtonGap;                       // 56
constexpr int16_t kSideButtonY0 = arrocco::kScreenH - 16 - 3 * kSideButtonStep + kButtonGap;  // 304
constexpr int16_t kSideButtonX0 = kSideInnerX;
constexpr int16_t kSideButtonX1 = kSideInnerX + kSideInnerW - kSideButtonW;  // 644
constexpr Rect sideButtonRect(int slot) {
  return Rect{(slot % 2) ? kSideButtonX1 : kSideButtonX0,
              static_cast<int16_t>(kSideButtonY0 + (slot / 2) * kSideButtonStep), kSideButtonW,
              kSideButtonH};
}
constexpr int sideButtonAt(int16_t x, int16_t y) {
  for (int slot = 0; slot < kSideButtonSlots; ++slot)
    if (sideButtonRect(slot).contains(x, y)) return slot;
  return -1;
}

// ---- full-screen menus (menu, clock picker, settings): a single column of big buttons
constexpr int kMenuSlots = 6;
constexpr int16_t kMenuTitleBaseline = 60;
constexpr int16_t kMenuSubtitleBaseline = 80;
constexpr int16_t kMenuButtonX = 200;
constexpr int16_t kMenuButtonW = 400;
constexpr int16_t kMenuButtonH = kMinButtonH;
constexpr int16_t kMenuButtonY0 = 96;
constexpr int16_t kMenuButtonStep = kMenuButtonH + kButtonGap;  // 56
constexpr int16_t kMenuFooterBaseline = 464;
constexpr Rect menuButtonRect(int slot) {
  return Rect{kMenuButtonX, static_cast<int16_t>(kMenuButtonY0 + slot * kMenuButtonStep),
              kMenuButtonW, kMenuButtonH};
}
constexpr int menuButtonAt(int16_t x, int16_t y) {
  for (int slot = 0; slot < kMenuSlots; ++slot)
    if (menuButtonRect(slot).contains(x, y)) return slot;
  return -1;
}

// ---- popups over the board -----------------------------------------------------------
constexpr int kPromotionChoices = 4;
constexpr int16_t kPromotionButton = 96;
constexpr int16_t kPromotionGap = 8;
constexpr Rect kPromotionBox{24, 152, 432, 160};
constexpr int16_t kPromotionTitleBaseline = 184;
constexpr Rect promotionButtonRect(int choice) {
  return Rect{static_cast<int16_t>(36 + choice * (kPromotionButton + kPromotionGap)), 200,
              kPromotionButton, kPromotionButton};
}

constexpr int kDialogMaxButtons = 3;
// Confirm popup: title, then three stacked kMinButtonH buttons, kButtonGap apart.
constexpr int16_t kConfirmButtonW = 320;
constexpr int16_t kConfirmButtonY0 = 56;   // first button top, below the box top
constexpr Rect kConfirmBox{64, 120, 352, kConfirmButtonY0 + 3 * (kMinButtonH + kButtonGap) + 8};  // 232 tall
constexpr Rect kGameOverBox{40, 136, 400, 208};

// ---- behaviour ---------------------------------------------------------------------------
constexpr int16_t kTapSlop = 16;                  // Up this close to Down still counts as a tap
constexpr uint32_t kPanelOffAfterMs = 3000;       // idle time before dropping the high voltage
constexpr uint32_t kTouchHoldMaxMs = 2000;        // a finger "down" longer than this is a lost Up
constexpr int kBatteryRepaintStep = 5;            // menu footer: repaint only for a change this big
constexpr uint32_t kBatteryRepaintMinMs = 30000;  // ... and not more often than this (USB: at once)
constexpr uint8_t kPartialsBeforeFull = 16;       // "normal" refresh policy
constexpr uint8_t kPartialsBeforeFullFewer = 32;  // "fewer flashes"

}  // namespace arrocco::ui
