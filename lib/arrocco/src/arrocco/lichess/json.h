// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco Lichess — just enough JSON to read the objects Lichess sends. No heap, no STL,
// no library: the payloads are small, flat and known, and a reader that only walks an object
// looking for one key is a hundred lines.
//
// Nothing is copied: a JsonValue is a span of the caller's buffer. Only copyString() copies,
// into a caller-sized array, and says so when the value did not fit.
//
// It is a READER, not a validator: it walks the members in order and stops at the one asked for,
// so a payload truncated AFTER that member still reads correctly. That is deliberate — an
// /api/account response is bigger than any buffer we want on the device, and "username" is the
// second field.
#pragma once
#include <stdint.h>

namespace arrocco::lichess {

struct JsonValue {
  enum class Type : uint8_t { None, Object, Array, String, Number, Bool, Null };

  const char* raw = nullptr;  // the value as written: a string still has its quotes and escapes
  int size = 0;
  Type type = Type::None;

  bool isNone() const { return type == Type::None; }
  bool isNull() const { return type == Type::Null; }
  bool boolValue() const { return type == Type::Bool && size > 0 && raw[0] == 't'; }
};

class JsonObject {
 public:
  JsonObject() = default;
  JsonObject(const char* text, int size) : text_(text), size_(size) {}

  bool valid() const { return text_ != nullptr && size_ > 0; }
  const char* text() const { return text_; }
  int size() const { return size_; }

  // The value of a member of THIS object (never of a nested one). False when absent.
  bool find(const char* key, JsonValue& out) const;
  bool has(const char* key) const;

  // The member as a nested object; invalid() when the member is absent or is not an object.
  JsonObject object(const char* key) const;

  // Decoded string member. False when absent, not a string, or too long for `out` — `out` then
  // holds what fitted, always NUL-terminated (and "" when the member is absent).
  bool copyString(const char* key, char* out, int outSize) const;
  // True when the member is a string equal to `expected`.
  bool stringIs(const char* key, const char* expected) const;

  // Integer member. Saturates at INT32_MIN/INT32_MAX, truncates a fractional part.
  bool number(const char* key, int32_t& out) const;
  bool boolean(const char* key, bool& out) const;

  static JsonObject fromValue(const JsonValue& v);
  static bool decodeString(const JsonValue& v, char* out, int outSize);
  static bool toNumber(const JsonValue& v, int32_t& out);

 private:
  const char* text_ = nullptr;
  int size_ = 0;
};

}  // namespace arrocco::lichess
