// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco — the seam between the portable core (rules + UI) and whatever runs it:
// the firmware on the XIAO ESP32-S3 Plus, or the simulator on the Mac.
// The core draws with Adafruit_GFX into an 800x480 1-bit frame buffer and asks the
// platform to push it to the panel. Nothing in the core may include Arduino.h directly.
#pragma once
#include <stdint.h>

class Adafruit_GFX;

namespace arrocco {

constexpr int16_t kScreenW = 800;
constexpr int16_t kScreenH = 480;

// Same values as GxEPD_BLACK / GxEPD_WHITE, and what GFXcanvas1 expects (0 = bit clear = black).
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;

// How the frame buffer reaches the glass. One present() per user-visible event.
enum class Refresh : uint8_t {
  Partial,   // fast differential refresh, ~0.45 s, no flash, leaves some ghosting
  Full,      // fast full refresh, ~1.2 s, one black flash, clears ghosting
  Deep       // slow multi-flash refresh, ~3 s, only at game start/end
};

struct TouchEvent {
  enum Type : uint8_t { Down, Move, Up };
  Type type;
  int16_t x, y;        // screen pixels, origin top-left, already rotated/mirrored
  uint32_t ms;         // platform millis() when the sample was taken
};

class Platform {
public:
  virtual ~Platform() = default;
  virtual Adafruit_GFX& gfx() = 0;                 // the whole 800x480 frame buffer
  virtual void present(Refresh kind) = 0;          // blocking until the panel has finished
  virtual void panelOff() = 0;                     // drop the panel's high voltage when idle
  virtual uint32_t millis() = 0;
  virtual void beep(uint16_t hz, uint16_t ms) = 0;
  virtual int batteryPercent() = 0;                // -1 = no gauge found
  virtual bool usbPowered() = 0;
};

// The application: owns the game and every screen. Implemented in arrocco/ui.
class App {
public:
  virtual ~App() = default;
  virtual void begin() = 0;                        // draw the first screen
  virtual void onTouch(const TouchEvent& e) = 0;
  virtual void tick() = 0;                         // called often; clocks, timeouts, deferred refreshes
};

}  // namespace arrocco
