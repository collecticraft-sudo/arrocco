// SPDX-License-Identifier: GPL-3.0-or-later
#include "esp32_platform.h"

#include <Preferences.h>

#include "config.h"
#include "gauge.h"
#include "gt911.h"
#include "log.h"
#include "panel.h"

namespace {

// The saved game's own NVS namespace: the WiFi network and the Lichess token keep theirs.
constexpr char kStoreNamespace[] = "arrocco-game"; // NVS namespaces stop at 15 characters

} // namespace

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

// ---------- the saved game: NVS ----------
//
// One key per blob, written whole. NVS makes each write atomic (a power cut leaves the
// old value or the new one) and spreads the wear by itself: it appends every write to its
// partition (default_16MB.csv: 20 KB, five 4 KB sectors) and erases a sector only when it
// is full of stale copies. A saved game is 27 bytes plus two a ply, which NVS stores in
// 32-byte entries (126 to a sector) plus two of bookkeeping. Estimated, not measured: a
// 40-move game saved after every ply writes about 470 entries, four sector erases spread
// over five sectors; a 100-move game about fifteen. Against ~100,000 erase cycles per
// sector that is tens of thousands of games. A write should take a few milliseconds, and
// some tens more when NVS has to erase a sector first: the SAVE line logs the real time.

size_t Esp32Platform::loadBlob(const char* key, uint8_t* out, size_t capacity) {
  const uint32_t start = ::millis();
  Preferences p;
  // Read-write: on a board that never stored anything this creates the namespace, where a
  // read-only open would log an nvs_open error at every boot. Opening writes nothing else.
  if (!p.begin(kStoreNamespace, false)) {
    logLine("SAVE  WARNING NVS namespace %s does not open: nothing read back", kStoreNamespace);
    return 0;
  }
  size_t size = 0;
  if (p.isKey(key)) { // asked first: getBytesLength() logs an error for a key that is not there
    size = p.getBytesLength(key);
    if (size == 0 || size > capacity || p.getBytes(key, out, capacity) != size) size = 0;
  }
  p.end();
  logLine("SAVE  read back '%s': %u bytes in %lu ms", key, static_cast<unsigned>(size),
          static_cast<unsigned long>(::millis() - start));
  return size;
}

bool Esp32Platform::storeBlob(const char* key, const uint8_t* data, size_t size) {
  const uint32_t start = ::millis();
  Preferences p;
  bool ok = p.begin(kStoreNamespace, false);
  if (ok) {
    ok = p.putBytes(key, data, size) == size;
    p.end();
  }
  const unsigned long took = static_cast<unsigned long>(::millis() - start);
  if (ok) {
    logLine("SAVE  wrote '%s': %u bytes in %lu ms", key, static_cast<unsigned>(size), took);
  } else {
    logLine("SAVE  WARNING '%s' not written (%u bytes, %lu ms): NVS refused it", key,
            static_cast<unsigned>(size), took);
  }
  return ok;
}

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
