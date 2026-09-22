// SPDX-License-Identifier: GPL-3.0-or-later
#include "screens.h"

#include <stdarg.h>

#include "config.h"
#include "gauge.h"
#include "gt911.h"
#include "i2cbus.h"
#include "log.h"
#include "panel.h"

namespace screens {
namespace {

using panel::display;

constexpr uint16_t kInk = GxEPD_BLACK;
constexpr uint16_t kPaper = GxEPD_WHITE;
constexpr int16_t kCharW = 12; // classic 6x8 font at size 2
constexpr int16_t kLineH = 20;
constexpr int16_t kSideTextX = cfg::kSideX + 8;

struct Rect {
  int16_t x, y, w, h;
};

// Side column, bottom: the four screen buttons. Above them: up to three action buttons.
// All 56 px tall (one board square). The lowest ones end 24 px above the 40 px corner
// targets of the Touch screen: a sloppy tap on target 4 must not land on "Sound".
constexpr Rect kNav[4] = {{488, 296, 148, 56}, {644, 296, 148, 56}, {488, 360, 148, 56}, {644, 360, 148, 56}};
constexpr Rect kAct[3] = {{488, 232, 96, 56}, {592, 232, 96, 56}, {696, 232, 96, 56}};
constexpr const char* kNames[4] = {"Board", "Touch", "Panel", "Sound"};
constexpr const char* kActLabel[4][3][2] = {
    {{"Reset", nullptr}, {nullptr, nullptr}, {nullptr, nullptr}},
    {{"Clear", nullptr}, {nullptr, nullptr}, {nullptr, nullptr}},
    {{"All", "black"}, {"All", "white"}, {"Full", "refresh"}},
    {{"Beep", nullptr}, {"Scale", nullptr}, {"Sweep", nullptr}}};
constexpr const char* kIntro[4] = {"tap a piece, then a target", "tap targets 1 2 3 4",
                                   "look for garbled pixels", "tap a sound button"};

// Touch screen corner targets: 1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right.
constexpr int16_t kTarget = 40;
constexpr Rect kCorner[4] = {{0, 0, kTarget, kTarget},
                             {cfg::kScreenW - kTarget, 0, kTarget, kTarget},
                             {0, cfg::kScreenH - kTarget, kTarget, kTarget},
                             {cfg::kScreenW - kTarget, cfg::kScreenH - kTarget, kTarget, kTarget}};

enum class Fill : uint8_t { None, Black, White };
struct Marker {
  int16_t x, y;
};
struct RawPoint {
  int32_t x, y;
};

Id s_id = Id::Touch;
Fill s_fill = Fill::None;
char s_status[27] = "";
char s_board[64]; // index = rank * 8 + file, a1 = 0; ' ' empty, upper case white, lower case black
int8_t s_selected = -1;
Marker s_markers[16];
uint8_t s_markerCount = 0;
uint8_t s_markerNext = 0;
RawPoint s_cornerRaw[4];
uint8_t s_cornerStep = 0;
char s_verdict[29] = "";

// ---------- small drawing helpers ----------

void setStatus(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void setStatus(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(s_status, sizeof(s_status), fmt, args);
  va_end(args);
}

// opaque = paint the character cells too (used where text sits on top of a pattern)
void text(int16_t x, int16_t y, uint8_t size, const char* s, uint16_t color = kInk, bool opaque = false) {
  display.setTextSize(size);
  if (opaque) display.setTextColor(color, color == kInk ? kPaper : kInk);
  else display.setTextColor(color);
  display.setCursor(x, y);
  display.print(s);
}

void sideLine(uint8_t row, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void sideLine(uint8_t row, const char* fmt, ...) {
  char buf[27]; // 26 characters x 12 px = the column width
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  text(kSideTextX, static_cast<int16_t>(8 + row * kLineH), 2, buf, kInk, true);
}

bool inside(const Rect& r, int16_t x, int16_t y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void drawButton(const Rect& r, const char* line1, const char* line2, bool active) {
  display.fillRect(r.x, r.y, r.w, r.h, active ? kInk : kPaper);
  display.drawRect(r.x, r.y, r.w, r.h, kInk);
  display.drawRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, kInk);
  const char* lines[2] = {line1, line2};
  const int16_t count = line2 ? 2 : 1;
  int16_t y = static_cast<int16_t>(r.y + (r.h - count * 18) / 2 + 1);
  for (int16_t i = 0; i < count; ++i, y += 18) {
    const int16_t w = static_cast<int16_t>(strlen(lines[i]) * kCharW);
    text(static_cast<int16_t>(r.x + (r.w - w) / 2), y, 2, lines[i], active ? kPaper : kInk);
  }
}

// Ordered 4x4 dither on a white background: level/16 of the pixels go black.
void ditherRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t level) {
  static const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  for (int16_t yy = y; yy < y + h; ++yy)
    for (int16_t xx = x; xx < x + w; ++xx)
      if (kBayer[yy & 3][xx & 3] < level) display.drawPixel(xx, yy, kInk);
}

void hatchRect(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int16_t yy = y; yy < y + h; ++yy)
    for (int16_t xx = x; xx < x + w; ++xx)
      if (((xx + yy) & 3) == 0) display.drawPixel(xx, yy, kInk);
}

// Placeholder piece: a letter in a 47 px disc with a white halo, so it reads on any square.
// White = white disc, black ring, black letter. Black = black disc, white letter.
void drawPiece(int16_t cx, int16_t cy, char piece) {
  const bool white = (piece >= 'A' && piece <= 'Z');
  display.fillCircle(cx, cy, 25, kPaper);
  display.fillCircle(cx, cy, 23, kInk);
  if (white) display.fillCircle(cx, cy, 20, kPaper);
  const char letter[2] = {static_cast<char>(white ? piece : piece - 'a' + 'A'), '\0'};
  text(static_cast<int16_t>(cx - 7), static_cast<int16_t>(cy - 10), 3, letter, white ? kInk : kPaper);
}

// ---------- Board ----------

void resetBoard() {
  static const char kBackRank[9] = "RNBQKBNR";
  memset(s_board, ' ', sizeof(s_board));
  for (uint8_t f = 0; f < 8; ++f) {
    s_board[f] = kBackRank[f];
    s_board[8 + f] = 'P';
    s_board[48 + f] = 'p';
    s_board[56 + f] = static_cast<char>(kBackRank[f] - 'A' + 'a');
  }
  s_selected = -1;
}

void drawBoard() {
  const int16_t span = 8 * cfg::kSquare;
  for (uint8_t row = 0; row < 8; ++row) {   // row 0 = rank 8, at the top
    for (uint8_t file = 0; file < 8; ++file) {
      const int16_t x = static_cast<int16_t>(cfg::kBoardX + file * cfg::kSquare);
      const int16_t y = static_cast<int16_t>(cfg::kBoardY + row * cfg::kSquare);
      const int8_t index = static_cast<int8_t>((7 - row) * 8 + file);
      if ((file + row) & 1) ditherRect(x, y, cfg::kSquare, cfg::kSquare, cfg::kDarkSquareLevel);
      if (index == s_selected) { // 4 px black frame with a 2 px white inner line
        for (int16_t i = 0; i < 6; ++i)
          display.drawRect(x + i, y + i, cfg::kSquare - 2 * i, cfg::kSquare - 2 * i, i < 4 ? kInk : kPaper);
      }
      if (s_board[index] != ' ') drawPiece(x + cfg::kSquare / 2, y + cfg::kSquare / 2, s_board[index]);
    }
  }
  // The frame is part of every repaint, so nothing can erase it.
  display.drawRect(cfg::kBoardX - 2, cfg::kBoardY - 2, span + 4, span + 4, kInk);
  display.drawRect(cfg::kBoardX - 1, cfg::kBoardY - 1, span + 2, span + 2, kInk);
  for (uint8_t i = 0; i < 8; ++i) {
    const char file[2] = {static_cast<char>('a' + i), '\0'};
    const char rank[2] = {static_cast<char>('8' - i), '\0'};
    // 14 px are left under the frame and the classic font's 'g' needs 16: its two top
    // rows are empty, so lifting that one glyph by 2 px keeps its tail on the screen.
    const int16_t lift = (file[0] == 'g') ? 2 : 0;
    text(static_cast<int16_t>(cfg::kBoardX + i * cfg::kSquare + 23), cfg::kBoardY + span + 2 - lift, 2, file);
    text(1, static_cast<int16_t>(cfg::kBoardY + i * cfg::kSquare + 21), 2, rank);
  }
}

void boardTouch(const TouchPoint& t, int8_t action) {
  if (action == 0) {
    resetBoard();
    setStatus("position reset");
    return;
  }
  char sq[3];
  if (!squareAt(t.x, t.y, sq)) return;
  const int8_t index = static_cast<int8_t>((sq[1] - '1') * 8 + (sq[0] - 'a'));
  if (s_selected < 0) {
    if (s_board[index] == ' ') {
      setStatus("%s is empty", sq);
    } else {
      s_selected = index;
      setStatus("selected %s", sq);
    }
  } else if (index == s_selected) {
    s_selected = -1;
    setStatus("deselected %s", sq);
  } else {
    const char from[3] = {static_cast<char>('a' + s_selected % 8), static_cast<char>('1' + s_selected / 8), '\0'};
    s_board[index] = s_board[s_selected];
    s_board[s_selected] = ' ';
    s_selected = -1;
    setStatus("moved %s-%s", from, sq);
  }
  logLine("BOARD %s", s_status);
}

// ---------- Touch ----------

// Works on RAW coordinates, so the answer does not depend on the flags compiled in.
void computeVerdict() {
  const int32_t rightX = s_cornerRaw[1].x - s_cornerRaw[0].x; // finger moved right: 1 -> 2
  const int32_t rightY = s_cornerRaw[1].y - s_cornerRaw[0].y;
  const bool swap = abs(rightY) > abs(rightX);
  const int32_t right = swap ? rightY : rightX;
  const int32_t down = swap ? (s_cornerRaw[2].x - s_cornerRaw[0].x) : (s_cornerRaw[2].y - s_cornerRaw[0].y);
  const int32_t right2 = swap ? (s_cornerRaw[3].y - s_cornerRaw[2].y) : (s_cornerRaw[3].x - s_cornerRaw[2].x);
  if (abs(right) < 200 || abs(down) < 200 || ((right > 0) != (right2 > 0))) {
    snprintf(s_verdict, sizeof(s_verdict), "unclear: tap 1 2 3 4 again");
    logLine("TOUCHTEST inconclusive: tap the corner targets 1, 2, 3, 4 in that order");
    return;
  }
  const int mirrorX = right < 0 ? 1 : 0;
  const int mirrorY = down < 0 ? 1 : 0;
  const bool flagsOk = (swap ? 1 : 0) == TOUCH_SWAP_XY && mirrorX == TOUCH_MIRROR_X && mirrorY == TOUCH_MIRROR_Y;
  if (flagsOk) snprintf(s_verdict, sizeof(s_verdict), "axes OK (S%d X%d Y%d)", swap ? 1 : 0, mirrorX, mirrorY);
  else snprintf(s_verdict, sizeof(s_verdict), "SET SWAP=%d MIRX=%d MIRY=%d", swap ? 1 : 0, mirrorX, mirrorY);
  logLine("TOUCHTEST panel needs TOUCH_SWAP_XY=%d TOUCH_MIRROR_X=%d TOUCH_MIRROR_Y=%d; built with %d %d %d: %s",
          swap ? 1 : 0, mirrorX, mirrorY, TOUCH_SWAP_XY, TOUCH_MIRROR_X, TOUCH_MIRROR_Y,
          flagsOk ? "OK" : "EDIT src/hwtest/config.h AND REBUILD");
}

void touchTouch(const TouchPoint& t, int8_t action) {
  if (action == 0) {
    s_markerCount = 0;
    s_markerNext = 0;
    s_cornerStep = 0;
    s_verdict[0] = '\0';
    setStatus("cleared");
    return;
  }
  s_markers[s_markerNext] = Marker{t.x, t.y};
  s_markerNext = static_cast<uint8_t>((s_markerNext + 1) % 16);
  if (s_markerCount < 16) ++s_markerCount;
  s_cornerRaw[s_cornerStep] = RawPoint{t.rawX, t.rawY};
  setStatus("target %u taken", s_cornerStep + 1);
  if (++s_cornerStep == 4) {
    s_cornerStep = 0;
    computeVerdict();
  }
}

void drawTouchGrid() { // dotted lines every 80 px, across the whole panel
  for (int16_t x = 0; x < cfg::kScreenW; x += 80)
    for (int16_t y = 0; y < cfg::kScreenH; y += 4) display.drawPixel(x, y, kInk);
  for (int16_t y = 0; y < cfg::kScreenH; y += 80)
    for (int16_t x = 0; x < cfg::kScreenW; x += 4) display.drawPixel(x, y, kInk);
}

void drawTouchOverlay(const TouchPoint& last) {
  display.fillRect(64, 160, 368, 116, kPaper);
  display.drawRect(64, 160, 368, 116, kInk);
  char buf[48];
  text(76, 170, 2, "Tap targets 1 2 3 4 in order");
  snprintf(buf, sizeof(buf), "next: target %u", s_cornerStep + 1);
  text(76, 170 + kLineH, 2, buf);
  if (last.valid) snprintf(buf, sizeof(buf), "raw %u,%u map %d,%d", last.rawX, last.rawY, last.x, last.y);
  else snprintf(buf, sizeof(buf), "raw -,-");
  text(76, 170 + 2 * kLineH, 2, buf);
  text(76, 170 + 3 * kLineH, 2, s_verdict);
  text(76, 170 + 4 * kLineH, 2, "marker far from finger = bad");

  // A target that holds a marker turns black: its number stays readable and a
  // target lighting up in the wrong corner is the sign of a mirrored axis.
  for (uint8_t i = 0; i < 4; ++i) {
    const Rect& r = kCorner[i];
    bool hit = false;
    for (uint8_t m = 0; m < s_markerCount; ++m) hit = hit || inside(r, s_markers[m].x, s_markers[m].y);
    display.fillRect(r.x, r.y, r.w, r.h, hit ? kInk : kPaper);
    display.drawRect(r.x, r.y, r.w, r.h, kInk);
    display.drawRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, kInk);
    const char digit[2] = {static_cast<char>('1' + i), '\0'};
    text(r.x + 13, r.y + 10, 3, digit, hit ? kPaper : kInk);
  }
  const uint8_t newest = static_cast<uint8_t>((s_markerNext + 15) % 16);
  for (uint8_t i = 0; i < s_markerCount; ++i) {
    const Marker& m = s_markers[i];
    bool inTarget = false;
    for (uint8_t c = 0; c < 4; ++c) inTarget = inTarget || inside(kCorner[c], m.x, m.y);
    if (inTarget) continue; // shown by the target itself
    display.drawFastHLine(m.x - 10, m.y, 21, kInk);
    display.drawFastVLine(m.x, m.y - 10, 21, kInk);
    display.drawCircle(m.x, m.y, 6, kInk);
    if (i == newest) { // the newest one stands out
      display.drawCircle(m.x, m.y, 12, kInk);
      display.fillCircle(m.x, m.y, 4, kInk);
    }
  }
}

// ---------- Panel ----------

void drawTestCard() {
  static const uint8_t kLevel[6] = {2, 4, 6, 8, 0, 16}; // 0 = diagonal hatch
  static const char* const kLabel[6] = {"12.5%", "25%", "37.5%", "50%", "hatch", "solid"};
  for (uint8_t i = 0; i < 6; ++i) {
    const int16_t x = static_cast<int16_t>(16 + (i % 3) * 152);
    const int16_t y = static_cast<int16_t>(16 + (i / 3) * 216);
    if (kLevel[i] == 0) hatchRect(x, y, 144, 168);
    else ditherRect(x, y, 144, 168, kLevel[i]);
    display.drawRect(x - 1, y - 1, 146, 170, kInk);
    drawPiece(x + 40, y + 84, 'N');
    drawPiece(x + 104, y + 84, 'n');
    text(x, y + 174, 2, kLabel[i]);
  }
  text(16, 440, 2, "Patches must be even and sharp.");
  text(16, 460, 2, "Garbled? Reseat the display FPC.");
}

Action panelTouch(int8_t action) {
  if (action == 0) s_fill = Fill::Black;
  if (action == 1) s_fill = Fill::White;
  if (action == 0 || action == 1) setStatus("tap anywhere to go back");
  if (action == 2) setStatus("full refresh requested");
  return action >= 0 ? Action::FullRefresh : Action::None;
}

// ---------- Sound ----------

void drawSoundHelp() {
  text(16, 24, 3, "Sound test");
  text(16, 72, 2, "KY-006 passive buzzer:");
  text(16, 72 + kLineH, 2, "  S -> D7 (GPIO44)   - -> GND");
  text(16, 72 + 3 * kLineH, 2, "Beep : 880 Hz, 80 ms (move click)");
  text(16, 72 + 4 * kLineH, 2, "Scale: C5 to C6, eight notes");
  text(16, 72 + 5 * kLineH, 2, "Sweep: 500 to 4000 Hz, find the");
  text(16, 72 + 6 * kLineH, 2, "       loudest pitch of this buzzer");
  text(16, 72 + 8 * kLineH, 2, "Silence? Check the S and - wires.");
}

Action soundTouch(int8_t action) {
  static const Action kSound[3] = {Action::Beep, Action::Scale, Action::Sweep};
  if (action < 0) return Action::None;
  const uint8_t index = static_cast<uint8_t>(action);
  setStatus("played %s", kActLabel[3][index][0]);
  return kSound[index];
}

// ---------- side column ----------

void drawSide(const TouchPoint& last) {
  const panel::Stats& st = panel::stats();
  char buf[40];
  display.drawFastVLine(cfg::kSideX, 0, cfg::kScreenH, kInk);
  sideLine(0, "%s", cfg::kTitle);
  gt911::headline(buf, sizeof(buf));
  sideLine(1, "%s", buf);
  sideLine(2, "%s", gt911::hint());
  if (last.valid) {
    char sq[3];
    squareAt(last.x, last.y, sq);
    sideLine(3, "raw %u,%u -> %s", last.rawX, last.rawY, sq);
  } else {
    sideLine(3, "raw -,- (no touch yet)");
  }
  const gt911::WireFault fault = gt911::wireFault();
  if (fault == gt911::WireFault::IntMissing) sideLine(4, "INT WIRE MISSING? A0-D9");
  else if (fault == gt911::WireFault::RstMissing) sideLine(4, "RST WIRE MISSING? A3-D6");
  else sideLine(4, "INT %lu  I2C err %lu", static_cast<unsigned long>(gt911::intEdges()),
                static_cast<unsigned long>(i2cbus::errors()));
  sideLine(5, "part %lums full %lums", static_cast<unsigned long>(st.lastPartialMs),
           static_cast<unsigned long>(st.lastFullMs));
  // total = what the user waits; BUSY = what the panel itself took (the rest is SPI)
  sideLine(6, "BUSY %lums %u/%u to full", static_cast<unsigned long>(st.lastBusyMs), st.partialSinceFull,
           cfg::kFullEvery);
  sideLine(7, "%s", gauge::text());
  sideLine(8, "heap %luk psram %luk", static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
           static_cast<unsigned long>(ESP.getFreePsram() / 1024));
  const i2cbus::Scan& scan = i2cbus::lastScan();
  size_t used = static_cast<size_t>(
      snprintf(buf, sizeof(buf), "%s", !scan.done ? "I2C: not scanned" : scan.stuck ? "I2C: SDA/SCL held LOW" : (scan.count ? "I2C:" : "I2C: bus empty")));
  for (uint8_t i = 0; i < scan.count && i < 6 && used < sizeof(buf) - 4; ++i)
    used += static_cast<size_t>(snprintf(buf + used, sizeof(buf) - used, " %02X", scan.addr[i]));
  sideLine(9, "%s", buf);
  sideLine(10, "%s", s_status);

  const uint8_t id = static_cast<uint8_t>(s_id);
  for (uint8_t i = 0; i < 3; ++i)
    if (kActLabel[id][i][0]) drawButton(kAct[i], kActLabel[id][i][0], kActLabel[id][i][1], false);
  for (uint8_t i = 0; i < 4; ++i) drawButton(kNav[i], kNames[i], nullptr, i == id);
}

} // namespace

void begin() {
  resetBoard();
  // Touch comes first: until the axes are proven right, no button can be trusted.
  show(Id::Touch);
}

void show(Id id) {
  s_id = id;
  s_fill = Fill::None;
  s_selected = -1;
  setStatus("%s", kIntro[static_cast<uint8_t>(id)]);
  logLine("SCREEN %s", name(id));
}

Id current() { return s_id; }
const char* name(Id id) { return kNames[static_cast<uint8_t>(id)]; }

bool squareAt(int16_t x, int16_t y, char out[3]) {
  const int16_t bx = static_cast<int16_t>(x - cfg::kBoardX);
  const int16_t by = static_cast<int16_t>(y - cfg::kBoardY);
  const int16_t span = 8 * cfg::kSquare;
  const bool onBoard = bx >= 0 && by >= 0 && bx < span && by < span;
  out[0] = onBoard ? static_cast<char>('a' + bx / cfg::kSquare) : '-';
  out[1] = onBoard ? static_cast<char>('8' - by / cfg::kSquare) : '-';
  out[2] = '\0';
  return onBoard;
}

Action onTouchDown(const TouchPoint& touch) {
  if (s_fill != Fill::None) { // all-black / all-white page: any touch brings the test card back
    s_fill = Fill::None;
    setStatus("%s", kIntro[static_cast<uint8_t>(Id::Panel)]);
    return Action::None;
  }
  for (uint8_t i = 0; i < 4; ++i) {
    if (inside(kNav[i], touch.x, touch.y)) {
      show(static_cast<Id>(i));
      return Action::None;
    }
  }
  int8_t action = -1;
  for (uint8_t i = 0; i < 3; ++i)
    if (kActLabel[static_cast<uint8_t>(s_id)][i][0] && inside(kAct[i], touch.x, touch.y)) action = static_cast<int8_t>(i);
  switch (s_id) {
    case Id::Board: boardTouch(touch, action); return Action::None;
    case Id::Touch: touchTouch(touch, action); return Action::None;
    case Id::Panel: return panelTouch(action);
    case Id::Sound: return soundTouch(action);
  }
  return Action::None;
}

void draw(const TouchPoint& lastTouch) {
  display.fillScreen(s_fill == Fill::Black ? kInk : kPaper);
  if (s_fill != Fill::None) return;
  switch (s_id) {
    case Id::Board: drawBoard(); break;
    case Id::Touch: drawTouchGrid(); break;
    case Id::Panel: drawTestCard(); break;
    case Id::Sound: drawSoundHelp(); break;
  }
  drawSide(lastTouch);
  if (s_id == Id::Touch) drawTouchOverlay(lastTouch); // targets and markers go on top of everything
}

} // namespace screens
