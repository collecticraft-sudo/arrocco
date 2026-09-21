// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — JSON-lines event writer. See protocol.h for the format.
#include "protocol.h"

#include <stdio.h>

namespace arrocco_sim {

namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 48,000 bytes -> 64,000 characters. Static so a frame never touches the heap.
constexpr size_t kBase64Chars = ((kFrameBytes + 2) / 3) * 4;
char g_base64[kBase64Chars + 1];

void encodeBase64(const uint8_t* in, size_t length, char* out) {
  size_t i = 0;
  size_t o = 0;
  for (; i + 2 < length; i += 3) {
    const uint32_t triple = (static_cast<uint32_t>(in[i]) << 16) |
                            (static_cast<uint32_t>(in[i + 1]) << 8) | in[i + 2];
    out[o++] = kBase64Alphabet[(triple >> 18) & 0x3F];
    out[o++] = kBase64Alphabet[(triple >> 12) & 0x3F];
    out[o++] = kBase64Alphabet[(triple >> 6) & 0x3F];
    out[o++] = kBase64Alphabet[triple & 0x3F];
  }
  if (i < length) {
    const bool two = (i + 1 < length);
    const uint32_t triple = (static_cast<uint32_t>(in[i]) << 16) |
                            (two ? static_cast<uint32_t>(in[i + 1]) << 8 : 0u);
    out[o++] = kBase64Alphabet[(triple >> 18) & 0x3F];
    out[o++] = kBase64Alphabet[(triple >> 12) & 0x3F];
    out[o++] = two ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=';
    out[o++] = '=';
  }
  out[o] = '\0';
}

// Commands typed by a person can contain anything; keep the output valid JSON.
void writeJsonString(const char* text) {
  fputc('"', stdout);
  for (const char* p = text; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '"' || c == '\\') {
      fputc('\\', stdout);
      fputc(c, stdout);
    } else if (c < 0x20 || c >= 0x7F) {
      fprintf(stdout, "\\u%04x", c);
    } else {
      fputc(c, stdout);
    }
  }
  fputc('"', stdout);
}

void endEvent() {
  fputc('\n', stdout);
  fflush(stdout);
}

}  // namespace

const char* refreshName(arrocco::Refresh kind) {
  switch (kind) {
    case arrocco::Refresh::Partial: return "partial";
    case arrocco::Refresh::Full:    return "full";
    case arrocco::Refresh::Deep:    return "deep";
  }
  return "partial";
}

void Emitter::hello(const char* appName, bool virtualTime) {
  fprintf(stdout, "{\"ev\":\"hello\",\"proto\":%d,\"app\":", kProtocolVersion);
  writeJsonString(appName);
  fprintf(stdout, ",\"w\":%d,\"h\":%d,\"virtual_time\":%s}", arrocco::kScreenW,
          arrocco::kScreenH, virtualTime ? "true" : "false");
  endEvent();
}

void Emitter::frame(const FrameInfo& info, const uint8_t* bits) {
  encodeBase64(bits, kFrameBytes, g_base64);
  fprintf(stdout,
          "{\"ev\":\"frame\",\"seq\":%u,\"kind\":\"%s\",\"t\":%u,\"nominal_ms\":%u,"
          "\"block_ms\":%u,\"power_on\":%s,\"resend\":%s,"
          "\"counts\":{\"partial\":%u,\"full\":%u,\"deep\":%u},\"since_full\":%u,"
          "\"w\":%d,\"h\":%d,\"data\":\"",
          info.seq, refreshName(info.kind), info.timeMs, info.nominalMs, info.blockMs,
          info.powerOn ? "true" : "false", info.resend ? "true" : "false",
          info.counts.partial, info.counts.full, info.counts.deep, info.sinceFull,
          arrocco::kScreenW, arrocco::kScreenH);
  fwrite(g_base64, 1, kBase64Chars, stdout);
  fputs("\"}", stdout);
  endEvent();
}

void Emitter::beep(uint16_t hz, uint16_t ms, uint32_t timeMs) {
  fprintf(stdout, "{\"ev\":\"beep\",\"hz\":%u,\"ms\":%u,\"t\":%u}", hz, ms, timeMs);
  endEvent();
}

void Emitter::panel(bool on, uint32_t timeMs) {
  fprintf(stdout, "{\"ev\":\"panel\",\"on\":%s,\"t\":%u}", on ? "true" : "false", timeMs);
  endEvent();
}

void Emitter::touchIgnored(uint32_t totalCount) {
  fprintf(stdout, "{\"ev\":\"touch_ignored\",\"count\":%u}", totalCount);
  endEvent();
}

void Emitter::state(int batteryPercent, bool usbPowered, double latencyScale) {
  fprintf(stdout, "{\"ev\":\"state\",\"battery\":%d,\"usb\":%s,\"scale\":%.3f}", batteryPercent,
          usbPowered ? "true" : "false", latencyScale);
  endEvent();
}

void Emitter::error(const char* message, const char* detail) {
  fputs("{\"ev\":\"error\",\"msg\":", stdout);
  writeJsonString(message);
  fputs(",\"detail\":", stdout);
  writeJsonString(detail ? detail : "");
  fputc('}', stdout);
  endEvent();
}

void Emitter::bye() {
  fputs("{\"ev\":\"bye\"}", stdout);
  endEvent();
}

}  // namespace arrocco_sim
