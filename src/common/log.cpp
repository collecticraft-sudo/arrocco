// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.h"

#include <stdarg.h>
#include <stdio.h>

void logLine(const char* fmt, ...) {
  static char buf[200];
  int n = snprintf(buf, sizeof(buf), "[%8lu] ", static_cast<unsigned long>(millis()));
  if (n < 0) n = 0;
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), fmt, args);
  va_end(args);
  Serial.println(buf);
}
