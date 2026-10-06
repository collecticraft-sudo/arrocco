// SPDX-License-Identifier: GPL-3.0-or-later
#include "gt911.h"

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_attr.h>
#include <string.h>

#include "config.h"
#include "i2cbus.h"
#include "log.h"

namespace gt911 {
namespace {

constexpr uint8_t kAddrIntLow = 0x5D;  // INT LOW while RST rises (what we ask for)
constexpr uint8_t kAddrIntHigh = 0x14; // INT HIGH while RST rises
constexpr uint16_t kRegProductId = 0x8140; // 4 ASCII bytes, then firmware version LE
constexpr uint16_t kRegStatus = 0x814E;
constexpr uint16_t kRegPoint1 = 0x814F;    // track id, xL, xH, yL, yH, sizeL, sizeH
constexpr size_t kConfigReadPiece = 62;    // Wire buffers 128 bytes: 186 = 3 x 62
constexpr size_t kConfigWritePiece = 32;
constexpr uint32_t kConfigSettleMs = 150;  // after Config_Fresh, before the first read-back
constexpr uint8_t kConfigReadTries = 10;   // ... then one more try every 100 ms

Info s_info = {Status::NotStarted, 0, false, false, false, false, "", 0, 0, 0, 0, 0, 0};
char s_hint[40] = ""; // the screen shows its first 26 characters
bool s_isrAttached = false;
bool s_intInRtc = false; // GPIO8 handed to the RTC domain for the sleep
volatile uint32_t s_intEdges = 0;
uint32_t s_frames = 0;

// What the address test proved at the last cold boot, kept through deep sleep: resume()
// cannot repeat the test (it resets the chip) and must not forget its result.
struct Saved {
  uint32_t magic;
  uint8_t addr;
  bool busSwapped;
  bool addrFollows;
};
constexpr uint32_t kSavedMagic = 0x6911A5EDu;
RTC_DATA_ATTR Saved s_saved = {0, 0, false, false};

RefreshTouch s_rec = {false, false, 0, 0, 0, 0, false};  // being filled during a refresh
RefreshTouch s_last = {false, false, 0, 0, 0, 0, false}; // the last finished one
bool s_recDown = false;            // the last report recorded in this refresh had a finger
uint32_t s_lastRefreshPollMs = 0;

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
  // On the GDEY075T7-T01 the GT911 runs INT in falling-edge mode (Module_Switch1 0x3D):
  // it pulls the line LOW to report and needs a pull-up to bring it back. Neither the
  // FTS02 nor the panel has one, so the ESP32 provides it: with the pull-down used
  // before, the first device saw a proven INT wire and 0 edges. A disconnected wire
  // now reads a steady HIGH, so "0 edges" still means "no wire".
  pinMode(cfg::kTouchInt, INPUT_PULLUP);
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
  if (!i2cbus::read16(s_info.addr, kConfigReg, conf, sizeof(conf))) return false;
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

// The status/hint a working chip gets, from its address and bus orientation.
void settleStatus() {
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
}

void attachIsr() {
  s_intEdges = 0;
  s_frames = 0;
  attachInterrupt(digitalPinToInterrupt(cfg::kTouchInt), onIntEdge, CHANGE);
  s_isrAttached = true;
}

void remember() {
  s_saved = Saved{kSavedMagic, s_info.addr, s_info.busSwapped, s_info.addrFollows};
}

uint16_t absDiff(uint16_t a, uint16_t b) { return static_cast<uint16_t>(a > b ? a - b : b - a); }

// One report into the refresh summary. Reports with no finger only say "lifted".
void record(const Frame& f) {
  if (!f.fresh) return;
  if (f.count == 0) {
    s_recDown = false;
    return;
  }
  s_recDown = true;
  if (!s_rec.seen) {
    s_rec.seen = true;
    s_rec.single = true;
    s_rec.x = f.x;
    s_rec.y = f.y;
    s_rec.maxDev = 0;
  } else {
    const uint16_t dev = absDiff(f.x, s_rec.x) > absDiff(f.y, s_rec.y) ? absDiff(f.x, s_rec.x)
                                                                         : absDiff(f.y, s_rec.y);
    if (dev > s_rec.maxDev) s_rec.maxDev = dev;
  }
  if (f.palm || f.count > 1) s_rec.single = false;
  if (s_rec.frames < 255) ++s_rec.frames;
}

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// One read of the whole block: 184 bytes, checksum, Config_Fresh.
bool readBlock(Config& out) {
  uint8_t all[kConfigLen + 2];
  for (size_t off = 0; off < sizeof(all); off += kConfigReadPiece) {
    const size_t n = (sizeof(all) - off < kConfigReadPiece) ? sizeof(all) - off : kConfigReadPiece;
    if (!i2cbus::read16(s_info.addr, static_cast<uint16_t>(kConfigReg + off), all + off, n)) return false;
  }
  memcpy(out.bytes, all, kConfigLen);
  out.checksum = all[kConfigLen];
  out.fresh = all[kConfigLen + 1];
  return true;
}

} // namespace

bool begin(bool verbose) {
  releaseSleepHold(); // a hold left from a sleep would swallow the whole reset sequence
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

  settleStatus();
  logLine("TOUCH OK at 0x%02X, SDA/SCL %s", s_info.addr, s_info.busSwapped ? "SWAPPED" : "normal");
  if (s_info.addr == kAddrIntHigh)
    logLine("TOUCH answers at 0x14: INT or RST wire not doing its job (A0=INT, A3=RST)");
  if (!s_info.addrFollows)
    logLine("TOUCH now tap the screen a few times: INT edges staying at 0 = INT wire, "
            "INT edges counting = RST wire");
  if (s_info.busSwapped)
    logLine("TOUCH answers only with SDA/SCL swapped: swap the A4/A5 wires");
  logIdentity();

  attachIsr();
  remember();
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

uint32_t frames() { return s_frames; }
uint32_t intEdges() { return s_intEdges; }

// Every report pulses INT, so a few polled frames with no edge at all convict the INT
// wire. If INT works and the address still did not follow our resets, it is the RST wire.
WireFault wireFault() {
  if (!ready() || s_frames < 3) return WireFault::None;
  if (s_intEdges == 0) return WireFault::IntMissing;
  return s_info.addrFollows ? WireFault::None : WireFault::RstMissing;
}

// ---- touches during a refresh ----------------------------------------------------------------

void refreshPoll() {
  if (!ready()) return;
  const uint32_t now = millis();
  if (now - s_lastRefreshPollMs < cfg::kTouchPollMs) return;
  s_lastRefreshPollMs = now;
  Frame f;
  if (read(f)) record(f);
}

bool flush() {
  bool held = false;
  if (ready()) {
    // What is latched now was made during the refresh (or is the touch that woke the
    // board): a live report, read rather than thrown away, so the caller can judge it.
    Frame f;
    if (read(f)) record(f);
    // Listening costs kTouchFlushMs after every refresh; it is only worth it when the
    // last report seen had a finger. With nothing latched, nobody touched the glass.
    if (s_recDown) {
      const uint32_t t0 = millis();
      while (millis() - t0 < cfg::kTouchFlushMs) {
        delay(cfg::kTouchPollMs);
        if (!read(f)) break;
        if (!f.fresh) continue;
        record(f);
        held = f.count > 0;
      }
    }
  }
  s_rec.held = held;
  s_last = s_rec;
  s_rec = RefreshTouch{false, false, 0, 0, 0, 0, false};
  s_recDown = false;
  return held;
}

const RefreshTouch& lastRefreshTouch() { return s_last; }

// ---- deep sleep ---------------------------------------------------------------------------------

void releaseSleepHold() {
  // Same level first, then the latch released: RST never dips, so the chip is not reset.
  pinMode(cfg::kTouchRst, OUTPUT);
  digitalWrite(cfg::kTouchRst, HIGH);
  gpio_hold_dis(static_cast<gpio_num_t>(cfg::kTouchRst));
  gpio_deep_sleep_hold_dis();
  // After an ext0 wake GPIO8 is still an RTC IO (with the RTC pull-up): make it a plain
  // input with the IO MUX pull-up again. The RTC pull-up bit stays set; it only acts on
  // the pad while the pad belongs to the RTC domain, i.e. during the next sleep.
  rtc_gpio_deinit(static_cast<gpio_num_t>(cfg::kTouchInt));
  pinMode(cfg::kTouchInt, INPUT_PULLUP);
  s_intInRtc = false;
}

void prepareForDeepSleep() {
  if (s_isrAttached) {
    detachInterrupt(digitalPinToInterrupt(cfg::kTouchInt));
    s_isrAttached = false;
  }
  pinMode(cfg::kTouchRst, OUTPUT);
  digitalWrite(cfg::kTouchRst, HIGH);
  const esp_err_t hold = gpio_hold_en(static_cast<gpio_num_t>(cfg::kTouchRst));
  gpio_deep_sleep_hold_en();

  // Pull-up first, then the pad switch: INT never floats on the way into the RTC domain.
  const gpio_num_t intPin = static_cast<gpio_num_t>(cfg::kTouchInt);
  esp_err_t rtc = rtc_gpio_pulldown_dis(intPin);
  if (rtc == ESP_OK) rtc = rtc_gpio_pullup_en(intPin);
  if (rtc == ESP_OK) rtc = rtc_gpio_init(intPin);
  if (rtc == ESP_OK) rtc = rtc_gpio_set_direction(intPin, RTC_GPIO_MODE_INPUT_ONLY);
  s_intInRtc = (rtc == ESP_OK);
  remember();
  if (hold != ESP_OK || rtc != ESP_OK)
    logLine("TOUCH WARNING sleep pins: RST hold %s, INT RTC pull-up %s", esp_err_to_name(hold),
            esp_err_to_name(rtc));
}

bool resume(bool verbose) {
  releaseSleepHold();
  if (s_saved.magic != kSavedMagic || s_saved.addr == 0) {
    if (verbose) logLine("TOUCH nothing remembered from before the sleep: full start, with a reset");
    return false;
  }
  s_info = Info{Status::NotStarted, s_saved.addr, s_saved.busSwapped, s_saved.addrFollows, true, true,
                "", 0, 0, 0, 0, 0, 0};
  setHint("");
  if (!i2cbus::start(s_info.busSwapped) || !i2cbus::probe(s_info.addr)) {
    logLine("TOUCH no answer at 0x%02X after the sleep: full start, with a reset", s_saved.addr);
    s_info.addr = 0;
    return false;
  }
  if (!identify()) {
    logLine("TOUCH 0x%02X answers after the sleep but its registers cannot be read: full start",
            s_info.addr);
    s_info.addr = 0;
    return false;
  }
  settleStatus();
  // The report that woke the board: the chip has held it, unread, since the touch.
  // Reading it here also acknowledges it, so the chip reports live again from now on.
  Frame f;
  if (read(f) && f.fresh) {
    if (f.count > 0)
      logLine("TOUCH the wake-up touch: raw (%u,%u), %u finger(s) - dropped, it is not a tap", f.x, f.y,
              f.count);
    else
      logLine("TOUCH the wake-up touch had already lifted");
  }
  attachIsr();
  logLine("TOUCH resumed at 0x%02X with no reset: the chip kept running through the sleep%s",
          s_info.addr, s_info.addrFollows ? "" : " (wires not proven at the last cold boot)");
  if (verbose) logIdentity();
  return true;
}

bool intHigh() {
  const gpio_num_t pin = static_cast<gpio_num_t>(cfg::kTouchInt);
  return (s_intInRtc ? rtc_gpio_get_level(pin) : gpio_get_level(pin)) != 0;
}

bool canWake() { return ready() && (s_info.addrFollows || s_intEdges > 0); }

// ---- the configuration block ---------------------------------------------------------------------

uint8_t checksumOf(const uint8_t* bytes) {
  uint8_t sum = 0;
  for (size_t i = 0; i < kConfigLen; ++i) sum = static_cast<uint8_t>(sum + bytes[i]);
  return static_cast<uint8_t>(~sum + 1);
}

bool readConfig(Config& out) {
  if (!ready()) return false;
  Config a;
  Config b;
  if (!readBlock(a) || !readBlock(b)) return false;
  // Config_Fresh may legitimately change between two reads; the rest may not.
  if (memcmp(a.bytes, b.bytes, kConfigLen) != 0 || a.checksum != b.checksum) return false;
  out = a;
  return true;
}

void logConfig(const Config& cfgBlock, const char* tag) {
  const uint8_t* b = cfgBlock.bytes;
  const uint8_t sum = checksumOf(b);
  const uint8_t version = b[kOffVersion];
  logLine("%s version %u ('%c'), %ux%u, %u touches | checksum stored 0x%02X, computed 0x%02X: %s"
          " | Config_Fresh %u",
          tag, version, (version >= 'A' && version <= 'Z') ? static_cast<char>(version) : '?',
          le16(b + kOffXMax), le16(b + kOffYMax), b[kOffTouchNumber] & 0x0F, cfgBlock.checksum, sum,
          cfgBlock.checksum == sum ? "valid" : "MISMATCH", cfgBlock.fresh);
  static const char* const kTrigger[4] = {"rising edge", "falling edge", "low level", "high level"};
  const uint8_t m1 = b[kOffModuleSwitch1];
  const uint8_t m2 = b[kOffModuleSwitch2];
  logLine("%s Module_Switch1 0x%02X: INT %s, X2Y %u, Sito %u, stretch %u, X2X %u, Y2Y %u"
          " | Module_Switch2 0x%02X: first-touch filter %s, touch keys %s",
          tag, m1, kTrigger[m1 & 0x03], (m1 >> 3) & 1u, (m1 >> 2) & 1u, (m1 >> 4) & 3u, (m1 >> 6) & 1u,
          (m1 >> 7) & 1u, m2, (m2 & 0x10) ? "off" : "on", (m2 & 0x01) ? "on" : "off");
  const uint8_t shake = b[kOffShakeCount];
  logLine("%s Screen_Touch_Level %u, Screen_Leave_Level %u (the finger signal that starts and ends"
          " a touch: lower = a lighter touch counts) | Shake_Count 0x%02X: press de-jitter %u,"
          " release %u | Large_Touch %u | Noise_Reduction %u",
          tag, b[kOffTouchLevel], b[kOffLeaveLevel], shake, shake & 0x0Fu, shake >> 4,
          b[kOffLargeTouch], b[kOffNoiseReduction] & 0x0Fu);
  const uint8_t filter = b[kOffFilter];
  logLine("%s Low_Power_Control %u s, then green mode (one scan every ~40 ms) | report period %u ms"
          " | x/y threshold %u/%u (0 = a still finger keeps reporting) | Filter 0x%02X: first %u,"
          " normal %u",
          tag, b[kOffLowPower] & 0x0Fu, 5u + (b[kOffRefreshRate] & 0x0Fu), b[kOffXThreshold],
          b[kOffYThreshold], filter, filter >> 6, filter & 0x3Fu);
  logLine("%s borders (x32) top %u bottom %u left %u right %u | drivers %u+%u, sensors %u+%u,"
          " base frequency %u Hz | Tx gain 0x%02X, Rx gain 0x%02X, dump shift 0x%02X | hopping 0x%02X",
          tag, b[kOffSpaceTopBottom] >> 4, b[kOffSpaceTopBottom] & 0x0Fu, b[kOffSpaceLeftRight] >> 4,
          b[kOffSpaceLeftRight] & 0x0Fu, b[kOffDriverGroupA] & 0x1Fu, b[kOffDriverGroupB] & 0x1Fu,
          b[kOffSensorNum] & 0x0Fu, b[kOffSensorNum] >> 4, le16(b + kOffBaseFreq), b[kOffTxGain],
          b[kOffRxGain], b[kOffDumpShift], b[kOffHoppingFlag]);
  for (size_t off = 0; off < kConfigLen; off += 32) {
    char hex[3 * 32 + 1];
    size_t used = 0;
    for (size_t i = off; i < off + 32 && i < kConfigLen; ++i)
      used += static_cast<size_t>(snprintf(hex + used, sizeof(hex) - used, "%02X ", b[i]));
    if (used > 0) hex[used - 1] = '\0';
    logLine("%s 0x%04X: %s", tag, static_cast<unsigned>(kConfigReg + off), hex);
  }
}

bool writeConfig(const uint8_t* bytes, Config& after) {
  if (!ready()) return false;
  for (size_t off = 0; off < kConfigLen; off += kConfigWritePiece) {
    const size_t n = (kConfigLen - off < kConfigWritePiece) ? kConfigLen - off : kConfigWritePiece;
    if (!i2cbus::write16(s_info.addr, static_cast<uint16_t>(kConfigReg + off), bytes + off, n)) {
      logLine("TOUCH config write FAILED at 0x%04X: Config_Fresh was not set, the chip keeps its block",
              static_cast<unsigned>(kConfigReg + off));
      return false;
    }
  }
  // Checksum and Config_Fresh in one transaction: from here the chip takes the block
  // and stores it in its flash.
  const uint8_t tail[2] = {checksumOf(bytes), 0x01};
  if (!i2cbus::write16(s_info.addr, kChecksumReg, tail, sizeof(tail))) {
    logLine("TOUCH config write FAILED on the checksum: the chip keeps its block");
    return false;
  }
  delay(kConfigSettleMs);
  for (uint8_t attempt = 0; attempt < kConfigReadTries; ++attempt) {
    if (readConfig(after)) return memcmp(after.bytes, bytes, kConfigLen) == 0 && after.checksum == tail[0];
    delay(100);
  }
  logLine("TOUCH config written, but it cannot be read back");
  return false;
}

} // namespace gt911
