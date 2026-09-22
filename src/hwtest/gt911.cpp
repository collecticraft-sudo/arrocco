// SPDX-License-Identifier: GPL-3.0-or-later
#include "gt911.h"

#include "config.h"
#include "i2cbus.h"
#include "log.h"

namespace gt911 {
namespace {

constexpr uint8_t kAddrIntLow = 0x5D;  // INT LOW while RST rises (what we ask for)
constexpr uint8_t kAddrIntHigh = 0x14; // INT HIGH while RST rises
constexpr uint16_t kRegConfig = 0x8047;    // read-only for us: version, X/Y max, touches, switch1
constexpr uint16_t kRegProductId = 0x8140; // 4 ASCII bytes, then firmware version LE
constexpr uint16_t kRegStatus = 0x814E;
constexpr uint16_t kRegPoint1 = 0x814F;    // track id, xL, xH, yL, yH, sizeL, sizeH

Info s_info = {Status::NotStarted, 0, false, false, false, false, "", 0, 0, 0, 0, 0, 0};
char s_hint[40] = ""; // the screen shows its first 26 characters
bool s_isrAttached = false;
volatile uint32_t s_intEdges = 0;
uint32_t s_frames = 0;

void IRAM_ATTR onIntEdge() { s_intEdges = s_intEdges + 1; }

// The chip latches its I2C address from INT while RST rises: LOW -> 0x5D, HIGH -> 0x14.
// Leaves RST driven HIGH for good: neither RST nor INT has a pull-up on the board.
void resetSequence(bool intHigh) {
  if (s_isrAttached) {
    detachInterrupt(digitalPinToInterrupt(cfg::kTouchInt));
    s_isrAttached = false;
  }
  pinMode(cfg::kTouchInt, OUTPUT);
  pinMode(cfg::kTouchRst, OUTPUT);
  digitalWrite(cfg::kTouchInt, intHigh ? HIGH : LOW);
  digitalWrite(cfg::kTouchRst, LOW);
  delay(20);
  digitalWrite(cfg::kTouchRst, HIGH); // the address is latched on this edge
  delay(10);
  digitalWrite(cfg::kTouchInt, LOW);
  delay(50);                          // INT must stay LOW a little longer
  // The GT911 drives INT push-pull from here on. The weak pull-down only makes a
  // disconnected wire read a steady LOW, so that "0 edges" really means "no wire".
  pinMode(cfg::kTouchInt, INPUT_PULLDOWN);
  delay(50);
}

void setHint(const char* text) { snprintf(s_hint, sizeof(s_hint), "%s", text); }

uint8_t probeBoth() {
  if (i2cbus::probe(kAddrIntLow)) return kAddrIntLow;
  if (i2cbus::probe(kAddrIntHigh)) return kAddrIntHigh;
  return 0;
}

bool identify() {
  uint8_t id[6] = {};
  uint8_t conf[7] = {};
  if (!i2cbus::read16(s_info.addr, kRegProductId, id, sizeof(id))) return false;
  if (!i2cbus::read16(s_info.addr, kRegConfig, conf, sizeof(conf))) return false;
  for (uint8_t i = 0; i < 4; ++i) {
    const char c = static_cast<char>(id[i]);
    s_info.productId[i] = (c == 0) ? '\0' : ((c >= 0x20 && c < 0x7F) ? c : '?');
  }
  s_info.productId[4] = '\0';
  s_info.firmware = static_cast<uint16_t>(id[4] | (id[5] << 8));
  s_info.configVersion = conf[0];
  s_info.xMax = static_cast<uint16_t>(conf[1] | (conf[2] << 8));
  s_info.yMax = static_cast<uint16_t>(conf[3] | (conf[4] << 8));
  s_info.maxTouches = conf[5] & 0x0F;
  s_info.moduleSwitch1 = conf[6];
  return true;
}

void logIdentity() {
  char bits[9];
  for (uint8_t i = 0; i < 8; ++i) bits[i] = (s_info.moduleSwitch1 & (0x80 >> i)) ? '1' : '0';
  bits[8] = '\0';
  static const char* const kTrigger[4] = {"rising", "falling", "low level", "high level"};
  logLine("TOUCH id \"%s\" fw 0x%04X config v%u, %ux%u, max %u touches", s_info.productId,
          s_info.firmware, s_info.configVersion, s_info.xMax, s_info.yMax, s_info.maxTouches);
  logLine("TOUCH Module_Switch1 = 0x%02X = 0b%s (INT trigger: %s, bit3 X2Y swap: %u)",
          s_info.moduleSwitch1, bits, kTrigger[s_info.moduleSwitch1 & 0x03],
          (s_info.moduleSwitch1 >> 3) & 1u);
  if (strncmp(s_info.productId, "911", 3) != 0)
    logLine("TOUCH WARNING product id is not \"911\": is this really the GT911 panel?");
}

} // namespace

bool begin(bool verbose) {
  s_info = Info{Status::NotStarted, 0, false, false, false, false, "", 0, 0, 0, 0, 0, 0};
  setHint("");
  const i2cbus::IdleLevels idle = i2cbus::idleLevels();
  s_info.sdaIdleHigh = idle.sdaHigh;
  s_info.sclIdleHigh = idle.sclHigh;
  resetSequence(false);

  for (uint8_t attempt = 0; attempt < 2 && s_info.addr == 0; ++attempt) {
    s_info.busSwapped = (attempt == 1);
    if (!i2cbus::start(s_info.busSwapped)) continue;
    s_info.addr = probeBoth();
  }

  // Wire proof: ask for the other address, then for 0x5D again. A chip that follows
  // both requests proves the RST wire (it resets) and the INT wire (it reads our level).
  if (s_info.addr != 0) {
    const uint8_t first = s_info.addr;
    resetSequence(true);
    const uint8_t second = probeBoth();
    resetSequence(false);
    s_info.addr = probeBoth();
    s_info.addrFollows = (first == kAddrIntLow && second == kAddrIntHigh && s_info.addr == kAddrIntLow);
    logLine("TOUCH address test: asked 0x5D, 0x14, 0x5D -> got 0x%02X, 0x%02X, 0x%02X: %s", first, second,
            s_info.addr, s_info.addrFollows ? "RST and INT wires proven"
                                            : "address does NOT follow: RST (A3-D6) or INT (A0-D9) wire");
    if (s_info.addr == 0) s_info.addr = first; // silent after the last reset: identify() will tell
  }

  if (s_info.addr == 0) {
    s_info.busSwapped = false;
    i2cbus::start(false); // leave the bus the normal way round for the scan and the gauge
    s_info.status = Status::NoAnswer;
    if (!idle.sdaHigh && !idle.sclHigh) setHint("SDA+SCL low: 3V3 missing?");
    else if (!idle.sdaHigh) setHint("SDA low: wire/3V3/pullup?");
    else if (!idle.sclHigh) setHint("SCL low: wire/3V3/pullup?");
    else setHint("no answer: flat? RST wire?");
    if (verbose) {
      logLine("TOUCH FAIL: no answer at 0x5D or 0x14, also with SDA/SCL swapped");
      logLine("TOUCH idle bus levels: SDA %s, SCL %s (both must be HIGH through the 10k pull-ups)",
              idle.sdaHigh ? "HIGH" : "LOW", idle.sclHigh ? "HIGH" : "LOW");
      logLine("TOUCH check: touch FPC seated (flat inserted flipped), 3.3V and GND on P11, "
              "A4=SDA, A5=SCL, A3=RST, A0=INT");
    }
    return false;
  }

  if (!identify()) {
    s_info.status = Status::IdError;
    setHint("answers, id unreadable");
    if (verbose) logLine("TOUCH FAIL: 0x%02X answers but its registers cannot be read", s_info.addr);
    return false;
  }

  s_info.status = Status::Ok;
  if (s_info.addr == kAddrIntHigh) {
    s_info.status = Status::OkAddr14;
    setHint("0x14: INT or RST wire bad");
  }
  if (s_info.busSwapped) {
    s_info.status = Status::OkSwapped;
    setHint("swap the A4/A5 wires");
  }
  if (s_info.status == Status::Ok && !s_info.addrFollows) setHint("addr stuck: RST/INT wire?");
  if (s_info.status == Status::Ok && s_info.addrFollows &&
      (s_info.xMax != cfg::kScreenW || s_info.yMax != cfg::kScreenH))
    snprintf(s_hint, sizeof(s_hint), "cfg %ux%u, not 800x480", s_info.xMax, s_info.yMax);

  logLine("TOUCH OK at 0x%02X, SDA/SCL %s", s_info.addr, s_info.busSwapped ? "SWAPPED" : "normal");
  if (s_info.addr == kAddrIntHigh)
    logLine("TOUCH answers at 0x14: INT or RST wire not doing its job (A0=INT, A3=RST)");
  if (!s_info.addrFollows)
    logLine("TOUCH now tap the screen a few times: INT edges staying at 0 = INT wire, "
            "INT edges counting = RST wire");
  if (s_info.busSwapped)
    logLine("TOUCH answers only with SDA/SCL swapped: swap the A4/A5 wires");
  logIdentity();

  s_intEdges = 0;
  s_frames = 0;
  attachInterrupt(digitalPinToInterrupt(cfg::kTouchInt), onIntEdge, CHANGE);
  s_isrAttached = true;
  return true;
}

bool ready() {
  return s_info.status == Status::Ok || s_info.status == Status::OkAddr14 ||
         s_info.status == Status::OkSwapped;
}

void markLost() {
  s_info.status = Status::Lost;
  setHint("lost: cable? retrying");
  logLine("TOUCH FAIL: %u I2C failures in a row, polling stopped, retrying begin() every %lu ms",
          cfg::kTouchLostAfter, static_cast<unsigned long>(cfg::kTouchRetryMs));
}

const Info& info() { return s_info; }

void headline(char* out, size_t len) {
  switch (s_info.status) {
    case Status::Ok:
    case Status::OkAddr14:
    case Status::OkSwapped:
      // at most 22 characters: the Touch screen's target 2 covers the end of this line
      snprintf(out, len, "%s 0x%02X id %s", (s_info.status == Status::Ok && s_info.addrFollows) ? "Touch OK" : "TOUCH WARN",
               s_info.addr, s_info.productId);
      break;
    case Status::NoAnswer: snprintf(out, len, "TOUCH FAIL no answer"); break;
    case Status::IdError: snprintf(out, len, "TOUCH FAIL at 0x%02X", s_info.addr); break;
    case Status::Lost: snprintf(out, len, "TOUCH FAIL I2C errors"); break;
    case Status::NotStarted: snprintf(out, len, "Touch not started"); break;
  }
}

const char* hint() { return s_hint; }

bool read(Frame& frame) {
  frame = Frame{false, false, 0, 0, 0, 0, 0};
  uint8_t status = 0;
  if (!i2cbus::read16(s_info.addr, kRegStatus, &status, 1)) return false;
  if ((status & 0x80) == 0) return true; // nothing new
  const uint8_t count = status & 0x0F;
  uint8_t p[7] = {};
  // A failed coordinate read is not acknowledged: the chip keeps the report for the next poll.
  if (count >= 1 && !i2cbus::read16(s_info.addr, kRegPoint1, p, sizeof(p))) return false;
  if (!i2cbus::write16(s_info.addr, kRegStatus, 0)) return false;
  frame.fresh = true;
  frame.palm = (status & 0x40) != 0;
  frame.count = count;
  frame.trackId = p[0];
  frame.x = static_cast<uint16_t>(p[1] | (p[2] << 8));
  frame.y = static_cast<uint16_t>(p[3] | (p[4] << 8));
  frame.size = static_cast<uint16_t>(p[5] | (p[6] << 8));
  ++s_frames;
  return true;
}

bool flush() {
  if (!ready()) return false;
  uint8_t status = 0;
  // 1) whatever was latched during the refresh is stale: acknowledge it unread
  if (i2cbus::read16(s_info.addr, kRegStatus, &status, 1) && (status & 0x80))
    i2cbus::write16(s_info.addr, kRegStatus, 0);
  // 2) reports arriving now are live: they tell whether the finger is still down
  bool held = false;
  const uint32_t t0 = millis();
  while (millis() - t0 < cfg::kTouchFlushMs) {
    delay(cfg::kTouchPollMs);
    if (!i2cbus::read16(s_info.addr, kRegStatus, &status, 1)) break;
    if ((status & 0x80) == 0) continue;
    held = (status & 0x0F) > 0;
    i2cbus::write16(s_info.addr, kRegStatus, 0);
  }
  return held;
}

uint32_t frames() { return s_frames; }
uint32_t intEdges() { return s_intEdges; }

// Every report pulses INT, so a few polled frames with no edge at all convict the INT
// wire. If INT works and the address still did not follow our resets, it is the RST wire.
WireFault wireFault() {
  if (!ready() || s_frames < 3) return WireFault::None;
  if (s_intEdges == 0) return WireFault::IntMissing;
  return s_info.addrFollows ? WireFault::None : WireFault::RstMissing;
}

} // namespace gt911
