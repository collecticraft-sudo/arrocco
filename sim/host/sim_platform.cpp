// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — SimPlatform implementation. See sim_platform.h.
#include "sim_platform.h"

#include <math.h>
#include <string.h>

namespace arrocco_sim {

namespace {

uint32_t nominalRefreshMs(arrocco::Refresh kind) {
  switch (kind) {
    case arrocco::Refresh::Partial: return kPartialMs;
    case arrocco::Refresh::Full:    return kFullMs;
    case arrocco::Refresh::Deep:    return kDeepMs;
  }
  return kPartialMs;
}

}  // namespace

SimPlatform::SimPlatform(Emitter& out, bool virtualTime)
    : out_(out),
      virtualTime_(virtualTime),
      canvas_(static_cast<uint16_t>(arrocco::kScreenW), static_cast<uint16_t>(arrocco::kScreenH)),
      start_(Clock::now()),
      busyUntil_(start_) {
  // GFXcanvas1 starts all zero bits, which is all black. Start from blank paper
  // instead; the app is expected to paint every pixel in begin() anyway.
  canvas_.fillScreen(arrocco::kWhite);
  memset(presented_, 0xFF, sizeof presented_);
}

uint32_t SimPlatform::millisAt(Clock::time_point when) const {
  if (virtualTime_) return virtualMs_;
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(when - start_);
  // Wraps after 49.7 days exactly like the uint32_t millis() on the device.
  return static_cast<uint32_t>(elapsed.count());
}

uint32_t SimPlatform::millis() { return millisAt(Clock::now()); }

void SimPlatform::present(arrocco::Refresh kind) {
  const bool powerOn = !panelOn_;
  const uint32_t nominal = nominalRefreshMs(kind) + (powerOn ? kPowerOnMs : 0);
  const uint32_t block = static_cast<uint32_t>(lround(nominal * latencyScale_));

  switch (kind) {
    case arrocco::Refresh::Partial: ++counts_.partial; ++sinceFull_; break;
    case arrocco::Refresh::Full:    ++counts_.full; sinceFull_ = 0; break;
    case arrocco::Refresh::Deep:    ++counts_.deep; sinceFull_ = 0; break;
  }

  memcpy(presented_, canvas_.getBuffer(), sizeof presented_);
  lastFrame_.seq = ++seq_;
  lastFrame_.kind = kind;
  lastFrame_.timeMs = millis();
  lastFrame_.nominalMs = nominal;
  lastFrame_.blockMs = block;
  lastFrame_.powerOn = powerOn;
  lastFrame_.resend = false;
  lastFrame_.counts = counts_;
  lastFrame_.sinceFull = sinceFull_;
  anyFrame_ = true;
  out_.frame(lastFrame_, presented_);

  if (powerOn) {
    panelOn_ = true;
    out_.panel(true, lastFrame_.timeMs);
  }

  if (virtualTime_) {
    // The core must see the clock jump by the real refresh time, scale or not:
    // that is what its timeouts and chess clocks will live with on the device.
    virtualMs_ += nominal;
  } else {
    blockFor(block);
  }
  busyUntil_ = Clock::now();
}

void SimPlatform::blockFor(uint32_t ms) {
  if (ms == 0) return;
  std::unique_lock<std::mutex> lock(stopMutex_);
  stopSignal_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return stopRequested_; });
}

void SimPlatform::requestStop() {
  {
    std::lock_guard<std::mutex> lock(stopMutex_);
    stopRequested_ = true;
  }
  stopSignal_.notify_all();
}

void SimPlatform::panelOff() {
  if (!panelOn_) return;
  panelOn_ = false;
  out_.panel(false, millis());
}

void SimPlatform::beep(uint16_t hz, uint16_t ms) {
  // tone() on the device does not block either: the buzzer plays while the code goes on.
  out_.beep(hz, ms, millis());
}

void SimPlatform::setBatteryPercent(int percent) {
  if (percent < -1) percent = -1;
  if (percent > 100) percent = 100;
  batteryPercent_ = percent;
}

void SimPlatform::setLatencyScale(double scale) {
  if (!(scale >= 0.0)) scale = 0.0;   // also catches NaN
  if (scale > 4.0) scale = 4.0;
  latencyScale_ = scale;
}

void SimPlatform::advanceVirtualTime(uint32_t ms) {
  if (virtualTime_) virtualMs_ += ms;
}

void SimPlatform::resendLastFrame() {
  if (!anyFrame_) return;
  FrameInfo again = lastFrame_;
  again.resend = true;
  out_.frame(again, presented_);
}

void SimPlatform::reportState() { out_.state(batteryPercent_, usbPowered_, latencyScale_); }

}  // namespace arrocco_sim
