// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

// The network code logs from four tasks (loop, request, stream reader, console worker).
// The line is built on the caller's own stack, so there is no buffer to share; the mutex
// only keeps two lines from interleaving on the wire. It is created on the first call,
// which happens in setup() while nothing else is running yet.
SemaphoreHandle_t s_lock = nullptr;

} // namespace

void logLine(const char* fmt, ...) {
  char buf[200];
  int n = snprintf(buf, sizeof(buf), "[%8lu] ", static_cast<unsigned long>(millis()));
  if (n < 0) n = 0;
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), fmt, args);
  va_end(args);

  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  const bool held = s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE;
  Serial.println(buf); // printed either way: a log line is never worth losing
  if (held) xSemaphoreGive(s_lock);
}
