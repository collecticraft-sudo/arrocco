// SPDX-License-Identifier: GPL-3.0-or-later
#include "panel.h"

#include <SPI.h>

#include "config.h"
#include "log.h"

namespace panel {

Epd display(Driver(cfg::kEpdCs, cfg::kEpdDc, cfg::kEpdRst, cfg::kEpdBusy));

namespace {
Stats s_stats = {0, 0, 0, 0, 0, 0, false, true, BusyPin::Unknown};
uint32_t s_busyTimeoutUs = kBusyTimeoutMs * 1000u;

// Time really spent waiting on BUSY. The total time of display() cannot tell a dead
// panel from a live one: pushing the 48,000-byte buffer over SPI takes 100+ ms per
// pass even with the FPC unplugged. The library calls this only while BUSY is active.
// Called about once a millisecond while a wait lasts: summing the short gaps between
// calls gives the time inside the waits and leaves out the SPI transfers between them.
// The longest single wait tells whether one of them ran into the library's timeout.
uint32_t s_busyUs = 0;
uint32_t s_busyLastUs = 0;
uint32_t s_waitUs = 0;    // the wait in progress
uint32_t s_maxWaitUs = 0; // the longest wait of this refresh

void onBusy(const void*) { // no SPI, no drawing, no logging in here
  const uint32_t now = micros();
  const uint32_t gap = now - s_busyLastUs;
  if (gap < 5000) {
    s_busyUs += gap;
    s_waitUs += gap;
    if (s_waitUs > s_maxWaitUs) s_maxWaitUs = s_waitUs;
  } else {
    s_waitUs = 0; // a new wait: the gap before it was an SPI transfer
  }
  s_busyLastUs = now;
  delay(1);
}

// The UC8179 drives BUSY push-pull (HIGH = idle). A pin that follows our own weak
// pull-up and pull-down is driven by nobody: the display FPC is not making contact.
BusyPin sampleBusyPin() {
  delay(20); // let the controller finish its own power-on reset
  pinMode(cfg::kEpdBusy, INPUT_PULLUP);
  delay(2);
  const bool withPullUp = digitalRead(cfg::kEpdBusy) == HIGH;
  pinMode(cfg::kEpdBusy, INPUT_PULLDOWN);
  delay(2);
  const bool withPullDown = digitalRead(cfg::kEpdBusy) == HIGH;
  pinMode(cfg::kEpdBusy, INPUT);
  if (withPullUp != withPullDown) return BusyPin::Floating;
  return withPullUp ? BusyPin::IdleHigh : BusyPin::StuckLow;
}
} // namespace

void begin() {
  // No MISO on this panel, and GPIO8 belongs to the touch INT: pins must be set
  // before display.init(), whose own SPI.begin() then becomes a no-op.
  SPI.begin(cfg::kEpdSck, -1, cfg::kEpdMosi, -1);
  // Serial diagnostics on (prints _PowerOn / _Update_Full / _Update_Part in microseconds),
  // initial = true, 10 ms reset pulse (RST is wired straight to the FPC), no pulldown mode.
  display.epd2.setBusyCallback(onBusy, nullptr);
  display.init(115200, true, 10, false);
  display.setRotation(cfg::kRotation);
  display.setFullWindow(); // once, for good
  display.setTextWrap(false);
  logLine("PANEL GDEY075T7 800x480 ready: CS %u DC %u RST %u BUSY %u SCK %u MOSI %u",
          cfg::kEpdCs, cfg::kEpdDc, cfg::kEpdRst, cfg::kEpdBusy, cfg::kEpdSck, cfg::kEpdMosi);
  s_stats.busyPin = sampleBusyPin(); // the panel has just been reset: BUSY must be driven HIGH
  if (s_stats.busyPin == BusyPin::IdleHigh) {
    logLine("PANEL BUSY pin is driven HIGH (idle): a panel is connected");
  } else if (s_stats.busyPin == BusyPin::Floating) {
    logLine("PANEL FAIL: BUSY pin FLOATS, nothing drives it. Display FPC not seated/latched, "
            "or XIAO not fully in its socket. The screen will stay blank.");
  } else {
    logLine("PANEL WARNING: BUSY pin reads LOW (busy) right after reset. If the refreshes below end "
            "in \"Busy Timeout!\" (10 s each): reseat the display FPC, check the driver board switch.");
  }
}

uint32_t refresh(Kind kind, uint32_t drawMs) {
  const bool powerOnIncluded = !s_stats.powered;
  s_busyUs = 0;
  s_waitUs = 0;
  s_maxWaitUs = 0;
  s_busyLastUs = micros() - 1000000u; // the first call must not count the gap before it
  const uint32_t t0 = millis();
  display.display(kind == Kind::Partial);
  const uint32_t ms = millis() - t0;
  const uint32_t busyMs = s_busyUs / 1000u;
  s_stats.lastBusyMs = busyMs;
  // The library stops a wait at the timeout: a single wait within 50 ms of it is one that
  // ran into it. With the 10 s default this is implied by the total-time rule below.
  const bool timedOut = s_maxWaitUs + 50000u >= s_busyTimeoutUs;

  if (kind == Kind::Partial) {
    s_stats.lastPartialMs = ms;
    ++s_stats.partialSinceFull;
    ++s_stats.partialTotal;
    s_stats.powered = true; // stays on until powerOff()
  } else {
    s_stats.lastFullMs = ms;
    s_stats.partialSinceFull = 0;
    ++s_stats.fullTotal;
    s_stats.powered = false; // the library powers the panel off after a full refresh
  }
  s_stats.responding = (busyMs >= cfg::kRefreshTooFastMs && ms <= cfg::kRefreshTooSlowMs && !timedOut);

  logLine("REFRESH %s #%lu: %lu ms, of which BUSY wait %lu ms (draw %lu ms, power-on %s), since full %u/%u",
          kind == Kind::Partial ? "partial" : kind == Kind::Full ? "full" : "deep",
          static_cast<unsigned long>(s_stats.partialTotal + s_stats.fullTotal),
          static_cast<unsigned long>(ms), static_cast<unsigned long>(busyMs),
          static_cast<unsigned long>(drawMs), powerOnIncluded ? "included" : "not needed",
          s_stats.partialSinceFull, cfg::kFullEvery);
  if (!s_stats.responding) {
    logLine("PANEL NOT RESPONDING: BUSY wait %lu ms, total %lu ms (a real refresh waits ~450 ms "
            "partial, 1200+ ms full)", static_cast<unsigned long>(busyMs), static_cast<unsigned long>(ms));
    logLine("PANEL check: display FPC seated and latched, driver board switch ON, "
            "XIAO fully pushed into its socket. %s",
            busyMs < cfg::kRefreshTooFastMs ? "BUSY never went active: FPC unplugged?"
                                            : "BUSY never released: see \"Busy Timeout!\" above.");
  }
  return ms;
}

bool fullDue() { return s_stats.partialSinceFull >= cfg::kFullEvery; }

void powerOff() {
  if (!s_stats.powered) return;
  const uint32_t t0 = millis();
  display.powerOff();
  s_stats.powered = false;
  logLine("PANEL power off after idle (%lu ms)", static_cast<unsigned long>(millis() - t0));
}

void setBusyTimeoutMs(uint32_t ms) {
  const uint32_t us = ms * 1000u;
  if (us == s_busyTimeoutUs) return;
  s_busyTimeoutUs = us;
  display.epd2.setBusyTimeoutUs(us);
  logLine("PANEL BUSY timeout now %lu ms per wait", static_cast<unsigned long>(ms));
}

const Stats& stats() { return s_stats; }

} // namespace panel
