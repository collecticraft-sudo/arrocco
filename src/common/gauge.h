// SPDX-License-Identifier: GPL-3.0-or-later
// Optional MAX17048 fuel gauge at 0x36 on the touch I2C bus.
#pragma once
#include <Arduino.h>

namespace gauge {

// Probes and, when present, reads the gauge. Logs only when `verbose` or on a change.
void poll(bool verbose);
bool present();
const char* text(); // "gauge 3.97V 84.2%" or "no gauge"
int percent();      // last state of charge, 0..100, or -1 when the gauge is absent

} // namespace gauge
