// SPDX-License-Identifier: GPL-3.0-or-later
// One printf-style line on the USB serial, prefixed with millis().
#pragma once
#include <Arduino.h>

// Uses one static buffer: call it from the main task only, never from an ISR.
void logLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
