// SPDX-License-Identifier: GPL-3.0-or-later
#include "esp32_platform.h"

#include "config.h"
#include "gauge.h"
#include "gt911.h"
#include "log.h"
#include "panel.h"

void Esp32Platform::begin() {
  buzzerOk_ = ledcAttach(cfg::kBuzzer, 1000, 10);
  if (buzzerOk_) ledcWriteTone(cfg::kBuzzer, 0); // duty 0: the pin rests LOW, the piezo draws nothing
  else logLine("SOUND WARNING ledcAttach(GPIO%u) failed: no beeps", cfg::kBuzzer);
}

Adafruit_GFX& Esp32Platform::gfx() { return panel::display; }

void Esp32Platform::present(arrocco::Refresh kind) {
  // A beep that started at the tap is still ringing: let it end (a few tens of ms)
  // rather than have it drone through a 450+ ms refresh, during which nothing runs.
  settleBeep();
  panel::Kind pk = panel::Kind::Partial;
  if (kind == arrocco::Refresh::Full) pk = panel::Kind::Full;
  if (kind == arrocco::Refresh::Deep) pk = panel::Kind::Deep; // same waveform as Full, see panel.h
  const uint32_t drawMs = ::millis() - eventMs_;
  panel::refresh(pk, drawMs); // blocks until BUSY releases, or until the timeout
  ++presents_;
  // A dead panel (FPC out, BUSY stuck) makes every wait run into the timeout, two or
  // three waits per refresh: keep them short until a refresh responds again. Nothing is
  // skipped: the buffer is always the whole screen, so the first refresh that works
  // shows the current state.
  panel::setBusyTimeoutMs(panel::stats().responding ? panel::kBusyTimeoutMs : kDeadPanelBusyMs);
  // Whatever was tapped while the glass was updating is discarded, never replayed.
  heldAfterRefresh_ = gt911::flush();
  refreshed_ = true;
}

void Esp32Platform::panelOff() { panel::powerOff(); }

uint32_t Esp32Platform::millis() { return ::millis(); }

// ---------- sound: a small queue stepped from the loop ----------

void Esp32Platform::beep(uint16_t hz, uint16_t ms) {
  if (!buzzerOk_ || ms == 0) return;
  if (count_ >= kQueueLen) return; // more than four pending: drop, never block
  queue_[static_cast<uint8_t>((head_ + count_) % kQueueLen)] = Beep{hz, ms};
  ++count_;
  if (tone_ == Tone::Idle) startNextBeep(::millis()); // heard at the tap, not after the refresh
}

void Esp32Platform::startNextBeep(uint32_t now) {
  const Beep b = queue_[head_];
  head_ = static_cast<uint8_t>((head_ + 1) % kQueueLen);
  --count_;
  ledcWriteTone(cfg::kBuzzer, b.hz);
  tone_ = Tone::Playing;
  toneStartMs_ = now;
  toneLenMs_ = b.ms;
}

void Esp32Platform::stepBeeps(uint32_t now) {
  switch (tone_) {
    case Tone::Idle:
      if (count_ > 0) startNextBeep(now);
      return;
    case Tone::Playing:
      if (now - toneStartMs_ < toneLenMs_) return;
      ledcWriteTone(cfg::kBuzzer, 0);
      tone_ = Tone::Gap;
      toneStartMs_ = now;
      return;
    case Tone::Gap:
      if (now - toneStartMs_ < kGapMs) return;
      tone_ = Tone::Idle;
      if (count_ > 0) startNextBeep(now);
      return;
  }
}

void Esp32Platform::settleBeep() {
  if (tone_ != Tone::Playing) return;
  const uint32_t elapsed = ::millis() - toneStartMs_;
  if (elapsed < toneLenMs_) delay(toneLenMs_ - elapsed); // bounded by the longest beep (80 ms)
  ledcWriteTone(cfg::kBuzzer, 0);
  tone_ = Tone::Gap;
  toneStartMs_ = ::millis(); // the queued ones resume from the loop, after the refresh
}

// ---------- power ----------

int Esp32Platform::batteryPercent() { return gauge::percent(); }

// The XIAO ESP32-S3 (Plus) exposes no VBUS-sense GPIO, and the e-paper driver board
// does not route its 5 V rail to any header pin: nothing on this hardware can tell USB
// power from battery power. The USB-CDC "host connected" state is not a substitute (a
// charger or power bank enumerates nothing). Hardware limit: always false.
bool Esp32Platform::usbPowered() { return false; }

// ---------- loop services ----------

void Esp32Platform::service() {
  const uint32_t now = ::millis();
  stepBeeps(now);
  if (now - lastGaugeMs_ >= cfg::kGaugePollMs) { // setup() probed it once; then every 10 s
    lastGaugeMs_ = now;
    gauge::poll(false);
  }
}

void Esp32Platform::markEvent(uint32_t now) { eventMs_ = now; }

bool Esp32Platform::takeRefreshNotice(bool& held) {
  if (!refreshed_) return false;
  refreshed_ = false;
  held = heldAfterRefresh_;
  return true;
}
