// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

// The network code logs from four tasks (loop, request, stream reader, console worker).
// The mutex keeps two lines from interleaving on the wire and guards the one static
// buffer they share. It is created on the first call, which happens in setup() while
// nothing else is running yet.
SemaphoreHandle_t s_lock = nullptr;
char s_line[512]; // only touched while s_lock is held

void build(char* buf, size_t size, const char* fmt, va_list args) {
  int n = snprintf(buf, size, "[%8lu] ", static_cast<unsigned long>(millis()));
  if (n < 0) n = 0;
  if (static_cast<size_t>(n) >= size) n = static_cast<int>(size - 1);
  vsnprintf(buf + n, size - static_cast<size_t>(n), fmt, args);
}

} // namespace

void logLine(const char* fmt, ...) {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  va_list args;
  va_start(args, fmt);
  if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    build(s_line, sizeof(s_line), fmt, args);
    Serial.println(s_line);
    xSemaphoreGive(s_lock);
  } else {
    // Someone has held the port for 50 ms (a host that stopped reading, most likely):
    // print anyway, from this task's own stack, shorter.
    char buf[200];
    build(buf, sizeof(buf), fmt, args);
    Serial.println(buf);
  }
  va_end(args);
}
