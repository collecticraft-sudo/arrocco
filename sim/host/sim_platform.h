// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — arrocco::Platform for the Mac. The frame buffer is the real
// GFXcanvas1 from Adafruit GFX, so every pixel and glyph the core draws here is the
// one the panel will get. present() hands the 48,000-byte buffer to the protocol and
// then blocks for as long as the real panel would, because a core that is never made
// to wait would hide exactly the problems the simulator exists to show.
#pragma once

#include <stdint.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <Adafruit_GFX.h>
#include <arrocco/platform.h>

#include "protocol.h"

namespace arrocco_sim {

// Refresh times of the GDEY075T7 under GxEPD2, from docs/ricerca/gxepd2.json:
// differential ~435-450 ms whatever the window, fast full ~1.17-1.2 s, and about
// 130-140 ms more when the high voltage has to be switched back on first.
constexpr uint32_t kPartialMs = 450;
constexpr uint32_t kFullMs = 1200;
constexpr uint32_t kDeepMs = 3000;
constexpr uint32_t kPowerOnMs = 140;

class SimPlatform final : public arrocco::Platform {
public:
  using Clock = std::chrono::steady_clock;

  // virtualTime: millis() only moves when told to (tick MS) or by present(), and
  // nothing sleeps. Makes scripted runs reproducible to the byte.
  SimPlatform(Emitter& out, bool virtualTime);

  // arrocco::Platform
  Adafruit_GFX& gfx() override { return canvas_; }
  void present(arrocco::Refresh kind) override;
  void panelOff() override;
  uint32_t millis() override;
  void beep(uint16_t hz, uint16_t ms) override;
  int batteryPercent() override { return batteryPercent_; }
  bool usbPowered() override { return usbPowered_; }

  // Simulator-side controls (what the hardware would decide, the UI decides here).
  void setBatteryPercent(int percent);
  void setUsbPowered(bool powered) { usbPowered_ = powered; }
  void setLatencyScale(double scale);
  void advanceVirtualTime(uint32_t ms);
  void resendLastFrame();
  void reportState();

  bool virtualTime() const { return virtualTime_; }
  // Wall-clock moment the last present() returned: input that arrived before it
  // happened while the panel was busy.
  Clock::time_point busyUntil() const { return busyUntil_; }
  uint32_t millisAt(Clock::time_point when) const;

  // Called from the stdin thread: cut short a present() that is still blocking.
  void requestStop();

private:
  void blockFor(uint32_t ms);

  Emitter& out_;
  const bool virtualTime_;
  GFXcanvas1 canvas_;
  uint8_t presented_[kFrameBytes];   // copy of what is "on the glass"
  FrameInfo lastFrame_{};
  bool anyFrame_ = false;

  const Clock::time_point start_;
  Clock::time_point busyUntil_;
  uint32_t virtualMs_ = 0;

  RefreshCounts counts_;
  uint32_t sinceFull_ = 0;
  uint32_t seq_ = 0;
  bool panelOn_ = false;             // like the device after boot: high voltage off
  int batteryPercent_ = 80;
  bool usbPowered_ = false;
  double latencyScale_ = 1.0;

  std::mutex stopMutex_;
  std::condition_variable stopSignal_;
  bool stopRequested_ = false;
};

}  // namespace arrocco_sim
