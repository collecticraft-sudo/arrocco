// SPDX-License-Identifier: GPL-3.0-or-later
// One printf-style line on the USB serial, prefixed with millis().
#pragma once
#include <Arduino.h>

// Lines up to 511 characters (the STAT line, the OAuth URL, the GT911 config dump). The
// line is built in one static buffer under a mutex, so any task may call it - the network
// code logs from four. If the mutex is held for more than 50 ms the line is built on the
// caller's own stack instead and cut at 199 characters: a log line is never worth losing.
// Never from an ISR: it takes a mutex and writes to USB.
void logLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
