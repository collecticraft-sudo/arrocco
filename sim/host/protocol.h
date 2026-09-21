// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — the line protocol between arrocco-sim and whatever drives it
// (sim/server.py, a test script, or a person typing in a terminal).
//
//   stdin, one command per line (plain text, so a human can type it):
//     touch down X Y | touch move X Y | touch up X Y     finger, in panel pixels
//     tick [MS]            run App::tick() once; with --virtual-time first advance
//                          the clock by MS milliseconds (default 0)
//     set battery N        -1 (no gauge) .. 100
//     set usb 0|1
//     set scale F          latency scale for present(): 1 = real, 0.25 = fast, 0 = none
//     frame                emit the last presented frame again ("resend":true)
//     quit
//
//   stdout, one JSON object per line ("ev" tells which):
//     hello  {proto, app, w, h, virtual_time}
//     frame  {seq, kind: partial|full|deep, t, nominal_ms, block_ms, power_on,
//             counts:{partial,full,deep}, since_full, resend, data: base64 of the
//             48,000-byte 1-bit buffer, row-major, MSB first, bit 1 = white}
//     beep   {hz, ms, t}          panel {on, t}        touch_ignored {count}
//     state  {battery, usb, scale}                     error {msg}      bye {}
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <arrocco/platform.h>

namespace arrocco_sim {

constexpr int kProtocolVersion = 1;
constexpr size_t kFrameBytes = static_cast<size_t>((arrocco::kScreenW + 7) / 8) * arrocco::kScreenH;

struct RefreshCounts {
  uint32_t partial = 0;
  uint32_t full = 0;
  uint32_t deep = 0;
};

struct FrameInfo {
  uint32_t seq;
  arrocco::Refresh kind;
  uint32_t timeMs;      // platform millis() when present() was called
  uint32_t nominalMs;   // what the real panel would take, power-on included
  uint32_t blockMs;     // what the simulator actually blocks for (nominal x scale)
  bool powerOn;         // the panel's high voltage was off, so it paid the power-on cost
  bool resend;          // an old frame emitted again, not a new refresh
  RefreshCounts counts;
  uint32_t sinceFull;   // partial refreshes since the last full or deep one
};

// Writes events to stdout. Only ever used from the main thread, so no locking;
// every event ends with a flush because the reader on the other side is a pipe.
class Emitter {
public:
  void hello(const char* appName, bool virtualTime);
  void frame(const FrameInfo& info, const uint8_t* bits);
  void beep(uint16_t hz, uint16_t ms, uint32_t timeMs);
  void panel(bool on, uint32_t timeMs);
  void touchIgnored(uint32_t totalCount);
  void state(int batteryPercent, bool usbPowered, double latencyScale);
  void error(const char* message, const char* detail);
  void bye();
};

const char* refreshName(arrocco::Refresh kind);

}  // namespace arrocco_sim
