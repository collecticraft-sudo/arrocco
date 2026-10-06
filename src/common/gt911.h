// SPDX-License-Identifier: GPL-3.0-or-later
// GT911 touch controller. Reads reports, survives a deep sleep of the ESP32 without being
// reset, and can read - and, only when asked on the serial console, write - its
// configuration block. Nothing in here writes the configuration on its own.
#pragma once
#include <Arduino.h>

namespace gt911 {

enum class Status : uint8_t {
  NotStarted,
  Ok,        // answers at 0x5D on the normal bus
  OkAddr14,  // works, but latched address 0x14: INT was not LOW while RST rose
  OkSwapped, // works only with SDA and SCL exchanged
  NoAnswer,  // nobody at 0x5D or 0x14, either way round
  IdError,   // answers, but the product id / config could not be read
  Lost,      // was working, then too many I2C failures in a row
};

struct Info {
  Status status;
  uint8_t addr;
  bool busSwapped;
  bool addrFollows; // the address followed our 0x5D / 0x14 / 0x5D resets: RST and INT wires proven
  bool sdaIdleHigh; // bus lines sampled before the probe
  bool sclIdleHigh;
  char productId[5];
  uint16_t firmware;
  uint8_t configVersion;
  uint16_t xMax;
  uint16_t yMax;
  uint8_t maxTouches;
  uint8_t moduleSwitch1;
};

struct Frame {
  bool fresh;   // the chip had a new report ("buffer ready")
  bool palm;    // large-area touch
  uint8_t count;
  uint8_t trackId;
  uint16_t x;
  uint16_t y;
  uint16_t size;
};

// Hardware reset + probe (0x5D, 0x14, then both again with SDA/SCL swapped) + address test
// (proves the RST and INT wires) + identification.
bool begin(bool verbose);
bool ready();    // true while the chip may be polled
void markLost(); // stops the polling; begin() is retried by the caller
const Info& info();
void headline(char* out, size_t len); // "Touch OK 0x5D id 911" / "TOUCH FAIL ..."
const char* hint();                   // what to check, "" when all is well

// One poll. false = I2C failure (frame unusable, nothing acknowledged).
bool read(Frame& frame);

uint32_t frames();   // fresh reports seen by polling
uint32_t intEdges(); // edges counted on the INT wire by the ISR
enum class WireFault : uint8_t { None, IntMissing, RstMissing };
WireFault wireFault(); // named once a few touches have been polled

// ---- touches during a screen refresh ---------------------------------------------------
//
// The chip keeps the FIRST report it makes after the host acknowledged the last one, and
// holds it - pulsing INT - until the host reads it (GT911 Programming Guide, section 5).
// So whatever is read during or right after a refresh is a live touch, never an old one.

// What the glass saw while the panel refreshed. Coordinates are raw GT911 ones.
struct RefreshTouch {
  bool seen;      // at least one report with a finger
  bool single;    // every such report had one finger and no large-area flag
  uint8_t frames; // reports with a finger
  uint16_t x;     // the first finger report: where the touch went down
  uint16_t y;
  uint16_t maxDev; // largest |dx| or |dy| of the later reports from that first point
  bool held;       // still on the glass when the refresh ended
};

// Reads one report during a refresh, at most every kTouchPollMs, and keeps it for the
// summary. Meant for panel::setBusyHook(); does nothing while the chip is not ready.
void refreshPoll();

// After a refresh: reads what is still latched (it used to be thrown away unread), then -
// only if a finger may still be there - listens for kTouchFlushMs. Returns `held`.
// Closes the summary that lastRefreshTouch() hands out.
bool flush();
const RefreshTouch& lastRefreshTouch();

// ---- deep sleep ----------------------------------------------------------------------------

// Last thing before esp_deep_sleep_start(). The chip stays powered and keeps scanning
// (green mode after Low_Power_Control seconds); its first report pulls INT LOW for a
// whole report period and wakes the ESP32 through ext0. RST is driven HIGH and held there
// through the sleep (GPIO43 is a digital pad: gpio_hold_en + gpio_deep_sleep_hold_en),
// INT gets the RTC-domain pull-up (the IO MUX one is off in deep sleep). The CHANGE
// interrupt is detached first. A report that is pending and unread keeps INT pulsing, so
// a touch made while the board was falling asleep wakes it at once.
void prepareForDeepSleep();

// First thing after a deep-sleep wake, instead of begin(): the chip never lost power and
// was never reset, and a reset now would recalibrate its baseline with the waking finger
// possibly still on the glass (it samples the "no touch" reference in its first 200 ms).
// Probes the address remembered in RTC memory, identifies, reads and drops the report
// that woke the board. false = the chip did not answer as expected: call begin().
bool resume(bool verbose);

// Gives GPIO43 (RST) and GPIO8 (INT) back to plain GPIO use after a deep sleep: RST
// driven HIGH again, then the hold released, so the line never dips. begin() and
// resume() call it; harmless when nothing is held.
void releaseSleepHold();

// INT as the pin reads now (true = HIGH, the idle level between reports).
bool intHigh();
// The chip can wake the board: INT and RST proven by the address test (at the last
// cold boot), or INT seen pulsing since this boot.
bool canWake();

// ---- the configuration block ------------------------------------------------------------
//
// 0x8047..0x80FE (184 bytes), checksum at 0x80FF, Config_Fresh at 0x8100 (GT911
// Programming Guide 2014-08-04, section 3.2 and 4.4). The rules, from that guide and the
// GT911 datasheet rev.10:
//  - Config_Chksum = two's complement of the 8-bit sum of the 184 bytes.
//  - A change counts only once the checksum is written and Config_Fresh is set to 1.
//  - Config_Version (0x8047): the chip takes a block whose version is HIGHER than its own,
//    or EQUAL to it with different contents; a lower version is ignored. 0x00 resets the
//    version to 'A'. The commands here always keep the version the chip already has.
//  - "Stationary configuration": the chip keeps what it was sent in its own flash. A write
//    survives power-off, and the only way back is writing the old block again - which is
//    why the commands keep a copy of the factory block (NVS) before the first write.
constexpr uint16_t kConfigReg = 0x8047;
constexpr size_t kConfigLen = 184;
constexpr uint16_t kChecksumReg = 0x80FF;

// Offsets in the block of the fields the firmware decodes or may change.
constexpr size_t kOffVersion = 0x8047 - kConfigReg;
constexpr size_t kOffXMax = 0x8048 - kConfigReg;          // 2 bytes, low first
constexpr size_t kOffYMax = 0x804A - kConfigReg;
constexpr size_t kOffTouchNumber = 0x804C - kConfigReg;
constexpr size_t kOffModuleSwitch1 = 0x804D - kConfigReg; // Y2Y X2X stretch X2Y Sito INT-mode
constexpr size_t kOffModuleSwitch2 = 0x804E - kConfigReg; // bit4 FirstFilter_Dis, bit0 touch key
constexpr size_t kOffShakeCount = 0x804F - kConfigReg;    // de-jitter: release (7:4), press (3:0)
constexpr size_t kOffFilter = 0x8050 - kConfigReg;        // first filter (7:6), normal filter (5:0)
constexpr size_t kOffLargeTouch = 0x8051 - kConfigReg;
constexpr size_t kOffNoiseReduction = 0x8052 - kConfigReg;
constexpr size_t kOffTouchLevel = 0x8053 - kConfigReg;    // Screen_Touch_Level: higher = harder
constexpr size_t kOffLeaveLevel = 0x8054 - kConfigReg;    // Screen_Leave_Level
constexpr size_t kOffLowPower = 0x8055 - kConfigReg;      // (3:0) seconds before green mode
constexpr size_t kOffRefreshRate = 0x8056 - kConfigReg;   // (3:0) report period = 5 + N ms
constexpr size_t kOffXThreshold = 0x8057 - kConfigReg;    // 0 = report a still finger continuously
constexpr size_t kOffYThreshold = 0x8058 - kConfigReg;
constexpr size_t kOffSpaceTopBottom = 0x805B - kConfigReg;
constexpr size_t kOffSpaceLeftRight = 0x805C - kConfigReg;
constexpr size_t kOffDriverGroupA = 0x8062 - kConfigReg;
constexpr size_t kOffDriverGroupB = 0x8063 - kConfigReg;
constexpr size_t kOffSensorNum = 0x8064 - kConfigReg;
constexpr size_t kOffBaseFreq = 0x8067 - kConfigReg;      // 2 bytes, low first, in Hz
constexpr size_t kOffTxGain = 0x806B - kConfigReg;        // (2:0) DAC gain, 0 = max
constexpr size_t kOffRxGain = 0x806C - kConfigReg;        // (2:0) PGA gain
constexpr size_t kOffDumpShift = 0x806D - kConfigReg;     // raw data amplification 2^N
constexpr size_t kOffHoppingFlag = 0x807D - kConfigReg;

struct Config {
  uint8_t bytes[kConfigLen];
  uint8_t checksum; // as stored at 0x80FF
  uint8_t fresh;    // Config_Fresh as read
};

uint8_t checksumOf(const uint8_t* bytes); // over kConfigLen bytes
// Reads the block twice; true when both reads agree byte for byte. `out.checksum` is the
// one stored in the chip: a block whose checksum does not match checksumOf(out.bytes) is
// shown, but never used as the base of a write.
bool readConfig(Config& out);
// Logs the decoded fields (what they mean, from the Programming Guide) and a hex dump.
// `tag` starts every line ("TOUCH cfg", "TOUCH factory").
void logConfig(const Config& cfg, const char* tag);
// Writes `bytes` (version byte included, unchanged by the caller) in 32-byte pieces,
// then [checksum, 1] at 0x80FF, waits for the chip, reads the block back into `after`.
// true = the chip now holds exactly `bytes`. Only the serial commands call this.
bool writeConfig(const uint8_t* bytes, Config& after);

} // namespace gt911
