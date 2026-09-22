// SPDX-License-Identifier: GPL-3.0-or-later
#include "arrocco/lichess/json.h"

namespace arrocco::lichess {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

const char* skipWs(const char* p, const char* end) {
  while (p < end && isSpace(*p)) ++p;
  return p;
}

// `p` is at the opening quote; returns the position just after the closing one, or nullptr.
const char* skipString(const char* p, const char* end) {
  if (p >= end || *p != '"') return nullptr;
  ++p;
  while (p < end) {
    if (*p == '\\') {
      p += 2;
      continue;
    }
    if (*p == '"') return p + 1;
    ++p;
  }
  return nullptr;
}

const char* skipValue(const char* p, const char* end) {
  p = skipWs(p, end);
  if (p >= end) return nullptr;
  if (*p == '"') return skipString(p, end);
  if (*p == '{' || *p == '[') {
    int depth = 0;
    while (p < end) {
      const char c = *p;
      if (c == '"') {
        p = skipString(p, end);
        if (p == nullptr) return nullptr;
        continue;
      }
      if (c == '{' || c == '[') {
        ++depth;
      } else if (c == '}' || c == ']') {
        --depth;
        if (depth <= 0) return p + 1;
      }
      ++p;
    }
    return nullptr;
  }
  const char* start = p;
  while (p < end && *p != ',' && *p != '}' && *p != ']' && !isSpace(*p)) ++p;
  return p == start ? nullptr : p;
}

JsonValue::Type classify(char c) {
  switch (c) {
    case '{': return JsonValue::Type::Object;
    case '[': return JsonValue::Type::Array;
    case '"': return JsonValue::Type::String;
    case 't':
    case 'f': return JsonValue::Type::Bool;
    case 'n': return JsonValue::Type::Null;
    default: return JsonValue::Type::Number;
  }
}

// Lichess keys hold no escapes, so a plain comparison is enough.
bool keyEquals(const char* start, int length, const char* key) {
  int i = 0;
  for (; i < length; ++i) {
    if (key[i] == '\0' || key[i] != start[i]) return false;
  }
  return key[i] == '\0';
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Appends one code point as UTF-8. Returns false when it did not fit.
bool appendUtf8(char* out, int outSize, int& n, uint32_t cp) {
  if (cp < 0x80) {
    if (n + 1 >= outSize) return false;
    out[n++] = static_cast<char>(cp);
  } else if (cp < 0x800) {
    if (n + 2 >= outSize) return false;
    out[n++] = static_cast<char>(0xC0 | (cp >> 6));
    out[n++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    if (n + 3 >= outSize) return false;
    out[n++] = static_cast<char>(0xE0 | (cp >> 12));
    out[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[n++] = static_cast<char>(0x80 | (cp & 0x3F));
  }
  return true;
}

}  // namespace

bool JsonObject::find(const char* key, JsonValue& out) const {
  out = JsonValue();
  if (!valid() || key == nullptr) return false;
  const char* const end = text_ + size_;
  const char* p = skipWs(text_, end);
  if (p >= end || *p != '{') return false;
  ++p;
  for (;;) {
    p = skipWs(p, end);
    if (p >= end || *p != '"') return false;  // '}' or a truncated payload
    const char* keyStart = p + 1;
    const char* afterKey = skipString(p, end);
    if (afterKey == nullptr) return false;
    const int keyLength = static_cast<int>(afterKey - 1 - keyStart);
    p = skipWs(afterKey, end);
    if (p >= end || *p != ':') return false;
    p = skipWs(p + 1, end);
    const char* valueStart = p;
    const char* afterValue = skipValue(p, end);
    if (afterValue == nullptr) return false;
    if (keyEquals(keyStart, keyLength, key)) {
      out.raw = valueStart;
      out.size = static_cast<int>(afterValue - valueStart);
      out.type = classify(*valueStart);
      return true;
    }
    p = skipWs(afterValue, end);
    if (p >= end || *p != ',') return false;
    ++p;
  }
}

bool JsonObject::has(const char* key) const {
  JsonValue v;
  return find(key, v);
}

JsonObject JsonObject::fromValue(const JsonValue& v) {
  if (v.type != JsonValue::Type::Object) return JsonObject();
  return JsonObject(v.raw, v.size);
}

JsonObject JsonObject::object(const char* key) const {
  JsonValue v;
  if (!find(key, v)) return JsonObject();
  return fromValue(v);
}

bool JsonObject::decodeString(const JsonValue& v, char* out, int outSize) {
  if (out == nullptr || outSize <= 0) return false;
  out[0] = '\0';
  if (v.type != JsonValue::Type::String || v.size < 2) return false;
  const char* p = v.raw + 1;                 // past the opening quote
  const char* const end = v.raw + v.size - 1;  // before the closing one
  int n = 0;
  while (p < end) {
    char c = *p++;
    if (c != '\\') {
      if (n + 1 >= outSize) {
        out[n] = '\0';
        return false;
      }
      out[n++] = c;
      continue;
    }
    if (p >= end) break;
    c = *p++;
    char plain = '\0';
    switch (c) {
      case 'n': plain = '\n'; break;
      case 't': plain = '\t'; break;
      case 'r': plain = '\r'; break;
      case 'b': plain = '\b'; break;
      case 'f': plain = '\f'; break;
      case 'u': {
        uint32_t cp = 0;
        if (end - p < 4) {
          out[n] = '\0';
          return false;
        }
        for (int i = 0; i < 4; ++i) {
          const int d = hexDigit(p[i]);
          if (d < 0) {
            out[n] = '\0';
            return false;
          }
          cp = (cp << 4) | static_cast<uint32_t>(d);
        }
        p += 4;
        // Surrogate halves are not worth the code on a chess clock: one '?' each.
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';
        if (!appendUtf8(out, outSize, n, cp)) {
          out[n] = '\0';
          return false;
        }
        continue;
      }
      default: plain = c; break;  // \" \\ \/ and anything else: itself
    }
    if (n + 1 >= outSize) {
      out[n] = '\0';
      return false;
    }
    out[n++] = plain;
  }
  out[n] = '\0';
  return true;
}

bool JsonObject::copyString(const char* key, char* out, int outSize) const {
  if (out != nullptr && outSize > 0) out[0] = '\0';
  JsonValue v;
  if (!find(key, v)) return false;
  return decodeString(v, out, outSize);
}

bool JsonObject::stringIs(const char* key, const char* expected) const {
  JsonValue v;
  if (!find(key, v) || v.type != JsonValue::Type::String || expected == nullptr) return false;
  const char* p = v.raw + 1;
  const char* const end = v.raw + v.size - 1;
  int i = 0;
  for (; p < end; ++p, ++i) {
    if (expected[i] == '\0' || expected[i] != *p) return false;
  }
  return expected[i] == '\0';
}

bool JsonObject::toNumber(const JsonValue& v, int32_t& out) {
  if (v.type != JsonValue::Type::Number || v.size <= 0) return false;
  const char* p = v.raw;
  const char* const end = v.raw + v.size;
  bool negative = false;
  if (*p == '-' || *p == '+') {
    negative = (*p == '-');
    ++p;
  }
  if (p >= end || *p < '0' || *p > '9') return false;
  int64_t value = 0;
  bool saturated = false;
  for (; p < end && *p >= '0' && *p <= '9'; ++p) {
    if (!saturated) {
      value = value * 10 + (*p - '0');
      if (value > 4294967296LL) saturated = true;  // far past int32: stop growing
    }
  }
  // Saturate BEFORE the sign is applied. Negating an already-saturated INT32_MIN produced
  // +2147483648, which the clamp below then turned into INT32_MAX: a huge NEGATIVE number came
  // back positive.
  if (saturated) {
    out = negative ? INT32_MIN : INT32_MAX;
    return true;
  }
  if (negative) value = -value;
  if (value > INT32_MAX) value = INT32_MAX;
  if (value < INT32_MIN) value = INT32_MIN;
  out = static_cast<int32_t>(value);
  return true;
}

bool JsonObject::number(const char* key, int32_t& out) const {
  JsonValue v;
  if (!find(key, v)) return false;
  return toNumber(v, out);
}

bool JsonObject::boolean(const char* key, bool& out) const {
  JsonValue v;
  if (!find(key, v) || v.type != JsonValue::Type::Bool) return false;
  out = v.boolValue();
  return true;
}

}  // namespace arrocco::lichess
