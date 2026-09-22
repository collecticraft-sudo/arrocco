// SPDX-License-Identifier: GPL-3.0-or-later
// One printf-style line on the USB serial, prefixed with millis().
#pragma once
#include <Arduino.h>

// Builds the line on the caller's own stack (200 bytes) and serialises the write with a
// mutex, so any task may call it - the network code logs from four. Never from an ISR:
// it takes a mutex and writes to USB.
void logLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
