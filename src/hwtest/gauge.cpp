// SPDX-License-Identifier: GPL-3.0-or-later
#include "gauge.h"

#include "i2cbus.h"
#include "log.h"

namespace gauge {
namespace {

constexpr uint8_t kAddr = 0x36;
constexpr uint8_t kRegVcell = 0x02; // 16-bit big-endian, 78.125 uV per bit
constexpr uint8_t kRegSoc = 0x04;   // 16-bit big-endian, 1/256 % per bit

bool s_present = false;
char s_text[27] = "no gauge";

} // namespace

void poll(bool verbose) {
  const bool wasPresent = s_present;
  uint8_t v[2] = {};
  uint8_t s[2] = {};
  s_present = i2cbus::probe(kAddr) && i2cbus::read8(kAddr, kRegVcell, v, 2) &&
              i2cbus::read8(kAddr, kRegSoc, s, 2);
  if (!s_present) {
    snprintf(s_text, sizeof(s_text), "no gauge");
    if (verbose || wasPresent) logLine("GAUGE no MAX17048 at 0x36 (optional)");
    return;
  }
  const uint32_t rawV = static_cast<uint32_t>((v[0] << 8) | v[1]);
  const uint32_t rawSoc = static_cast<uint32_t>((s[0] << 8) | s[1]);
  const uint32_t millivolts = rawV * 5u / 64u;   // 78.125 uV = 5/64 mV
  const uint32_t tenths = rawSoc * 10u / 256u;   // percent x 10
  snprintf(s_text, sizeof(s_text), "gauge %lu.%02luV %lu.%lu%%",
           static_cast<unsigned long>(millivolts / 1000u),
           static_cast<unsigned long>((millivolts % 1000u) / 10u),
           static_cast<unsigned long>(tenths / 10u), static_cast<unsigned long>(tenths % 10u));
  if (verbose || !wasPresent)
    logLine("GAUGE MAX17048: VCELL %lu mV, SOC %lu.%lu %%", static_cast<unsigned long>(millivolts),
            static_cast<unsigned long>(tenths / 10u), static_cast<unsigned long>(tenths % 10u));
}

bool present() { return s_present; }
const char* text() { return s_text; }

} // namespace gauge
