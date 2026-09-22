// SPDX-License-Identifier: GPL-3.0-or-later
// Esp32Platform: arrocco::Platform on the real hardware (XIAO ESP32-S3 Plus, GDEY075T7
// through GxEPD2, GT911 touch, KY-006 buzzer, optional MAX17048). It wraps the drivers
// in src/common/, shared with hwtest; the touch state machine lives in main.cpp.
#pragma once
#include <Arduino.h>

#include "arrocco/platform.h"

class Esp32Platform final : public arrocco::Platform {
 public:
  Esp32Platform() = default;

  // Attaches the buzzer. Call once from setup(), after the serial log is up.
  void begin();

  // arrocco::Platform
  Adafruit_GFX& gfx() override;
  void present(arrocco::Refresh kind) override; // blocks; flushes the GT911 afterwards
  void panelOff() override;
  uint32_t millis() override;
  void beep(uint16_t hz, uint16_t ms) override; // queued, never blocks
  int batteryPercent() override;                // last MAX17048 SOC, -1 without a gauge
  bool usbPowered() override;                   // always false: no VBUS sense, see .cpp

  // For the main loop.
  // Steps the beep queue and polls the gauge every 10 s. Reads the clock itself: the
  // loop's own `now` is stale after a present() (a tap ends in a 0.5-1.5 s refresh).
  void service();
  void markEvent(uint32_t now);    // start of the draw-time accounting for the next present()
  // True once after each present(); `held` = a finger was still on the glass when the
  // refresh ended (the touch state machine then ignores it until it lifts).
  bool takeRefreshNotice(bool& held);
  bool buzzerOk() const { return buzzerOk_; }
  uint32_t presents() const { return presents_; }

 private:
  struct Beep {
    uint16_t hz;
    uint16_t ms;
  };
  enum class Tone : uint8_t { Idle, Playing, Gap };
  static constexpr uint8_t kQueueLen = 4;
  static constexpr uint32_t kGapMs = 40; // silence between two queued beeps: keeps them two
  // BUSY timeout while the panel is not responding: a refresh then costs 6-9 s instead of
  // 20-30 s, and the loop is back to polling the touch and logging. Real waits on this
  // panel never exceed ~1.2 s (fast full refresh, fixed waveform timing), so a live panel
  // that comes back is recognised at once and gets the library's 10 s again.
  static constexpr uint32_t kDeadPanelBusyMs = 3000;

  void startNextBeep(uint32_t now);
  void stepBeeps(uint32_t now);
  void settleBeep(); // lets the beep in progress finish (<= its length) before a refresh

  Beep queue_[kQueueLen] = {};
  uint8_t head_ = 0;
  uint8_t count_ = 0;
  Tone tone_ = Tone::Idle;
  uint32_t toneStartMs_ = 0;
  uint32_t toneLenMs_ = 0;
  bool buzzerOk_ = false;

  bool refreshed_ = false;
  bool heldAfterRefresh_ = false;
  uint32_t presents_ = 0;
  uint32_t eventMs_ = 0;
  uint32_t lastGaugeMs_ = 0;
};
