// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — the demo App. It follows the screen rules of docs/decisioni.md
// (56 px squares from x=16,y=16, side column from x=480, one present() per visible
// event, a Full only every kFullEvery partials) so that what Fabrizio tries here
// already feels like the device; everything chess-specific is a placeholder.
#include "demo_app.h"

#include <stdio.h>
#include <string.h>

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

namespace arrocco_sim {

namespace {

using arrocco::kBlack;
using arrocco::kWhite;
using arrocco::Refresh;
using arrocco::TouchEvent;

// ---- strings (English only, all in one place) --------------------------------
constexpr char kTitle[] = "Arrocco";
constexpr char kSubtitle[] = "e-ink chess - simulator demo";
constexpr char kElapsedLabel[] = "Elapsed";
constexpr char kHintStart[] = "Tap a piece, then a square";
constexpr char kHintHold[] = "Hold a piece to remove it";
constexpr char kButtonFull[] = "Full refresh";
constexpr char kButtonNew[] = "New game";
constexpr char kClockOff[] = "--:--";

// ---- layout (docs/decisioni.md: everything on multiples of 8) ------------------
constexpr int16_t kSquare = 56;
constexpr int16_t kBoardX = 16;
constexpr int16_t kBoardY = 16;
constexpr int16_t kBoardPx = 8 * kSquare;        // 448
constexpr int16_t kSideX = 480;
constexpr int16_t kSideInnerX = kSideX + 16;     // 496
constexpr int16_t kSideInnerW = arrocco::kScreenW - 16 - kSideInnerX;  // 288
constexpr int16_t kButtonH = 48;
constexpr int16_t kButtonClockY = 296;
constexpr int16_t kButtonFullY = 352;
constexpr int16_t kButtonNewY = 408;

// ---- behaviour ------------------------------------------------------------------
constexpr uint8_t kFullEvery = 16;               // partial refreshes between two Full
constexpr uint32_t kLongPressMs = 600;
constexpr uint32_t kPanelOffAfterMs = 3000;      // idle time before dropping the high voltage
constexpr uint16_t kClockSteps[] = {10, 1, 0};   // seconds between clock refreshes; 0 = off

constexpr char kStartPosition[65] =
    "RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr";

enum class Target : uint8_t { None, Square, ButtonClock, ButtonFull, ButtonNew };

struct Hit {
  Target target = Target::None;
  int8_t square = -1;   // rank * 8 + file, only for Target::Square
  bool operator==(const Hit& other) const {
    return target == other.target && square == other.square;
  }
};

bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

class DemoApp final : public arrocco::App {
public:
  explicit DemoApp(arrocco::Platform& platform) : platform_(platform) {}

  void begin() override;
  void onTouch(const TouchEvent& e) override;
  void tick() override;

private:
  Hit hitTest(int16_t x, int16_t y) const;
  void activate(const Hit& hit);
  void newGame();
  void show(Refresh kind);
  Refresh movePauseRefresh();
  uint32_t clockStep(uint32_t now) const;

  void drawAll();
  void drawBoard();
  void drawSquare(int8_t square);
  void drawPiece(int16_t x, int16_t y, char piece);
  void drawSide();
  void drawButton(int16_t y, const char* label);
  void drawCentered(const char* text, int16_t cx, int16_t cy);

  arrocco::Platform& platform_;
  char squares_[64] = {};
  int8_t selected_ = -1;
  char lastMove_[8] = "";

  bool touchDown_ = false;
  bool longPressFired_ = false;
  Hit downHit_;
  uint32_t downMs_ = 0;

  uint32_t gameStartMs_ = 0;
  uint8_t clockStepIndex_ = 0;
  uint32_t shownClockStep_ = 0;
  int shownBattery_ = -2;
  bool shownUsb_ = false;

  uint8_t partialsSinceFull_ = 0;
  uint32_t lastPresentMs_ = 0;
  bool panelOffSent_ = true;
};

// ---- lifecycle ----------------------------------------------------------------------

void DemoApp::begin() {
  newGame();
}

void DemoApp::newGame() {
  memcpy(squares_, kStartPosition, sizeof squares_);
  selected_ = -1;
  lastMove_[0] = '\0';
  gameStartMs_ = platform_.millis();
  // Deep is reserved for game start/end: it is the one moment a 3 s flash is welcome.
  show(Refresh::Deep);
}

// Every visible change goes through here: redraw the whole buffer, push it once.
void DemoApp::show(Refresh kind) {
  const uint32_t now = platform_.millis();
  shownClockStep_ = clockStep(now);
  shownBattery_ = platform_.batteryPercent();
  shownUsb_ = platform_.usbPowered();
  if (kind == Refresh::Partial) {
    if (partialsSinceFull_ < 255) ++partialsSinceFull_;
  } else {
    partialsSinceFull_ = 0;
  }
  drawAll();
  platform_.present(kind);
  // present() blocks, so read the clock again rather than reuse 'now'.
  lastPresentMs_ = platform_.millis();
  panelOffSent_ = false;
}

// A finished move is a natural pause: the place to pay for a Full when ghosting
// has built up. Selections never do, because the second tap follows at once.
Refresh DemoApp::movePauseRefresh() {
  return partialsSinceFull_ >= kFullEvery ? Refresh::Full : Refresh::Partial;
}

uint32_t DemoApp::clockStep(uint32_t now) const {
  const uint16_t seconds = kClockSteps[clockStepIndex_];
  if (seconds == 0) return 0;
  return (now - gameStartMs_) / 1000u / seconds;
}

// ---- input ----------------------------------------------------------------------------

Hit DemoApp::hitTest(int16_t x, int16_t y) const {
  Hit hit;
  if (inRect(x, y, kBoardX, kBoardY, kBoardPx, kBoardPx)) {
    const int file = (x - kBoardX) / kSquare;
    const int rank = 7 - (y - kBoardY) / kSquare;   // rank 8 at the top
    hit.target = Target::Square;
    hit.square = static_cast<int8_t>(rank * 8 + file);
  } else if (inRect(x, y, kSideInnerX, kButtonClockY, kSideInnerW, kButtonH)) {
    hit.target = Target::ButtonClock;
  } else if (inRect(x, y, kSideInnerX, kButtonFullY, kSideInnerW, kButtonH)) {
    hit.target = Target::ButtonFull;
  } else if (inRect(x, y, kSideInnerX, kButtonNewY, kSideInnerW, kButtonH)) {
    hit.target = Target::ButtonNew;
  }
  return hit;
}

void DemoApp::onTouch(const TouchEvent& e) {
  switch (e.type) {
    case TouchEvent::Down:
      touchDown_ = true;
      longPressFired_ = false;
      downHit_ = hitTest(e.x, e.y);
      downMs_ = e.ms;
      break;
    case TouchEvent::Move:
      // Sliding off the target cancels the tap, as on any touch screen.
      if (touchDown_ && !(hitTest(e.x, e.y) == downHit_)) downHit_ = Hit{};
      break;
    case TouchEvent::Up: {
      const bool wasTap = touchDown_ && !longPressFired_ && downHit_.target != Target::None &&
                          hitTest(e.x, e.y) == downHit_;
      touchDown_ = false;
      if (wasTap) activate(downHit_);
      break;
    }
  }
}

void DemoApp::activate(const Hit& hit) {
  switch (hit.target) {
    case Target::Square: {
      const int8_t square = hit.square;
      if (selected_ < 0) {
        if (squares_[square] == '.') return;        // nothing to pick up: no refresh at all
        selected_ = square;
        show(Refresh::Partial);
      } else if (square == selected_) {
        selected_ = -1;
        show(Refresh::Partial);
      } else {
        snprintf(lastMove_, sizeof lastMove_, "%c%c-%c%c", 'a' + selected_ % 8,
                 '1' + selected_ / 8, 'a' + square % 8, '1' + square / 8);
        squares_[square] = squares_[selected_];
        squares_[selected_] = '.';
        selected_ = -1;
        platform_.beep(880, 30);
        show(movePauseRefresh());
      }
      break;
    }
    case Target::ButtonClock:
      clockStepIndex_ = static_cast<uint8_t>((clockStepIndex_ + 1) %
                                             (sizeof kClockSteps / sizeof kClockSteps[0]));
      show(Refresh::Partial);
      break;
    case Target::ButtonFull:
      show(Refresh::Full);
      break;
    case Target::ButtonNew:
      newGame();
      break;
    case Target::None:
      break;
  }
}

void DemoApp::tick() {
  const uint32_t now = platform_.millis();

  if (touchDown_) {
    // Long press is decided here, not on Up: the piece must vanish while the finger
    // is still down, which only a timer can do.
    if (!longPressFired_ && downHit_.target == Target::Square &&
        squares_[downHit_.square] != '.' && now - downMs_ >= kLongPressMs) {
      longPressFired_ = true;
      squares_[downHit_.square] = '.';
      if (selected_ == downHit_.square) selected_ = -1;
      platform_.beep(440, 60);
      show(Refresh::Partial);
    }
    // Never start a background refresh under a finger: it would swallow the tap.
    return;
  }

  const bool clockMoved = clockStep(now) != shownClockStep_;
  const bool powerChanged = platform_.batteryPercent() != shownBattery_ ||
                            platform_.usbPowered() != shownUsb_;
  if (clockMoved || powerChanged) {
    show(movePauseRefresh());
    return;
  }

  if (!panelOffSent_ && now - lastPresentMs_ >= kPanelOffAfterMs) {
    panelOffSent_ = true;
    platform_.panelOff();
  }
}

// ---- drawing ----------------------------------------------------------------------------

void DemoApp::drawAll() {
  Adafruit_GFX& gfx = platform_.gfx();
  gfx.setTextWrap(false);
  gfx.setTextSize(1);
  gfx.fillScreen(kWhite);
  drawBoard();
  drawSide();
}

void DemoApp::drawBoard() {
  Adafruit_GFX& gfx = platform_.gfx();
  for (int8_t square = 0; square < 64; ++square) drawSquare(square);
  gfx.drawRect(kBoardX - 1, kBoardY - 1, kBoardPx + 2, kBoardPx + 2, kBlack);

  // Coordinates live in the 16 px margins: ranks on the left, files under the board.
  // The classic 5x7 font doubled is 10x14 px, the largest that fits them.
  gfx.setFont(nullptr);
  gfx.setTextSize(2);
  gfx.setTextColor(kBlack);
  for (int i = 0; i < 8; ++i) {
    gfx.setCursor(2, static_cast<int16_t>(kBoardY + (7 - i) * kSquare + (kSquare - 14) / 2));
    gfx.write(static_cast<uint8_t>('1' + i));
    gfx.setCursor(static_cast<int16_t>(kBoardX + i * kSquare + (kSquare - 10) / 2),
                  kBoardY + kBoardPx + 2);
    gfx.write(static_cast<uint8_t>('a' + i));
  }
  gfx.setTextSize(1);
}

void DemoApp::drawSquare(int8_t square) {
  Adafruit_GFX& gfx = platform_.gfx();
  const int file = square % 8;
  const int rank = square / 8;
  const int16_t x = static_cast<int16_t>(kBoardX + file * kSquare);
  const int16_t y = static_cast<int16_t>(kBoardY + (7 - rank) * kSquare);
  const bool dark = ((file + rank) & 1) == 0;       // a1 is dark

  if (dark) {
    // 1-bit panel: "dark" is a 25 % diagonal hatch. Connected lines survive the fast
    // partial waveform better than isolated dots (docs/ricerca/gxepd2.json).
    for (int16_t py = 0; py < kSquare; ++py)
      for (int16_t px = 0; px < kSquare; ++px)
        if (((x + px + y + py) & 3) == 0) gfx.drawPixel(x + px, y + py, kBlack);
  }
  if (square == selected_) {
    for (int16_t inset = 1; inset <= 3; ++inset)
      gfx.drawRect(x + inset, y + inset, kSquare - 2 * inset, kSquare - 2 * inset, kBlack);
  }
  if (squares_[square] != '.') drawPiece(x, y, squares_[square]);
}

// Placeholder pieces: a letter in a disc. White = hollow disc, black = filled disc,
// both on a white halo so they stay readable on the hatch.
void DemoApp::drawPiece(int16_t x, int16_t y, char piece) {
  Adafruit_GFX& gfx = platform_.gfx();
  const bool white = piece >= 'A' && piece <= 'Z';
  const int16_t cx = x + kSquare / 2;
  const int16_t cy = y + kSquare / 2;
  constexpr int16_t kRadius = 20;

  gfx.fillCircle(cx, cy, kRadius + 2, kWhite);
  if (white) {
    gfx.drawCircle(cx, cy, kRadius, kBlack);
    gfx.drawCircle(cx, cy, kRadius - 1, kBlack);
  } else {
    gfx.fillCircle(cx, cy, kRadius, kBlack);
  }
  const char letter[2] = {static_cast<char>(white ? piece : piece - 'a' + 'A'), '\0'};
  gfx.setFont(&FreeSansBold18pt7b);
  gfx.setTextColor(white ? kBlack : kWhite);
  drawCentered(letter, cx, cy);
}

void DemoApp::drawSide() {
  Adafruit_GFX& gfx = platform_.gfx();
  char line[48];

  gfx.setTextColor(kBlack);
  gfx.setFont(&FreeSansBold18pt7b);
  gfx.setCursor(kSideInnerX, 48);
  gfx.print(kTitle);
  gfx.setFont(&FreeSans9pt7b);
  gfx.setCursor(kSideInnerX, 72);
  gfx.print(kSubtitle);
  gfx.drawFastHLine(kSideInnerX, 88, kSideInnerW, kBlack);

  // The clock shows the value it was last refreshed for, not "now": on e-ink a
  // number that is on the glass cannot be more current than its last refresh.
  gfx.setCursor(kSideInnerX, 116);
  gfx.print(kElapsedLabel);
  const uint16_t stepSeconds = kClockSteps[clockStepIndex_];
  if (stepSeconds == 0) {
    snprintf(line, sizeof line, "%s", kClockOff);
  } else {
    const uint32_t seconds = shownClockStep_ * stepSeconds;
    snprintf(line, sizeof line, "%02u:%02u", static_cast<unsigned>((seconds / 60) % 100),
             static_cast<unsigned>(seconds % 60));
  }
  gfx.setFont(&FreeSansBold24pt7b);
  gfx.setCursor(kSideInnerX, 160);
  gfx.print(line);
  gfx.drawFastHLine(kSideInnerX, 176, kSideInnerW, kBlack);

  gfx.setFont(&FreeSans9pt7b);
  gfx.setCursor(kSideInnerX, 204);
  if (lastMove_[0]) {
    snprintf(line, sizeof line, "Last move: %s", lastMove_);
    gfx.print(line);
  } else {
    gfx.print(kHintStart);
  }
  gfx.setCursor(kSideInnerX, 228);
  gfx.print(kHintHold);
  snprintf(line, sizeof line, "Partial since full: %u / %u",
           static_cast<unsigned>(partialsSinceFull_), static_cast<unsigned>(kFullEvery));
  gfx.setCursor(kSideInnerX, 252);
  gfx.print(line);
  if (shownBattery_ < 0) {
    snprintf(line, sizeof line, "Battery: no gauge%s", shownUsb_ ? "  (USB)" : "");
  } else {
    snprintf(line, sizeof line, "Battery: %d%%%s", shownBattery_, shownUsb_ ? "  (USB)" : "");
  }
  gfx.setCursor(kSideInnerX, 276);
  gfx.print(line);

  if (stepSeconds == 0) {
    snprintf(line, sizeof line, "Clock: off");
  } else {
    snprintf(line, sizeof line, "Clock: every %u s", static_cast<unsigned>(stepSeconds));
  }
  drawButton(kButtonClockY, line);
  drawButton(kButtonFullY, kButtonFull);
  drawButton(kButtonNewY, kButtonNew);
}

void DemoApp::drawButton(int16_t y, const char* label) {
  Adafruit_GFX& gfx = platform_.gfx();
  gfx.drawRoundRect(kSideInnerX, y, kSideInnerW, kButtonH, 8, kBlack);
  gfx.drawRoundRect(kSideInnerX + 1, y + 1, kSideInnerW - 2, kButtonH - 2, 7, kBlack);
  gfx.setFont(&FreeSansBold12pt7b);
  gfx.setTextColor(kBlack);
  drawCentered(label, kSideInnerX + kSideInnerW / 2, y + kButtonH / 2);
}

// Centres the glyphs' real bounding box, which is exact for every letter
// (baseline-based centring leaves Q and J visibly low).
void DemoApp::drawCentered(const char* text, int16_t cx, int16_t cy) {
  Adafruit_GFX& gfx = platform_.gfx();
  int16_t bx = 0;
  int16_t by = 0;
  uint16_t bw = 0;
  uint16_t bh = 0;
  gfx.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  gfx.setCursor(static_cast<int16_t>(cx - static_cast<int16_t>(bw) / 2 - bx),
                static_cast<int16_t>(cy - static_cast<int16_t>(bh) / 2 - by));
  gfx.print(text);
}

}  // namespace

arrocco::App* demoApp(arrocco::Platform& platform) {
  static DemoApp app(platform);
  return &app;
}

}  // namespace arrocco_sim
