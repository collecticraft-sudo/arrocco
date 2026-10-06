// SPDX-License-Identifier: GPL-3.0-or-later
#include "touch_tune.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>

#include "gt911.h"
#include "log.h"

namespace touch_tune {
namespace {

using gt911::kConfigLen;

constexpr char kNvsNamespace[] = "arrocco-gt911";
constexpr char kKeyFactory[] = "factory"; // the 184 bytes, then the checksum the chip had stored

// The lowest levels the commands accept: half the factory value, and never below these.
// Lower thresholds also let through the noise an e-paper refresh makes right under the
// touch layer (ghost touches), so the ladder stops at half.
constexpr uint8_t kTouchFloor = 20;
constexpr uint8_t kLeaveFloor = 10;
constexpr uint8_t kLadderPercent[] = {85, 70, 60, 50};

struct Backup {
  uint8_t bytes[kConfigLen];
  uint8_t checksum;
};

bool loadBackup(Backup& out) {
  Preferences p;
  // Read-write on purpose: a read-only open of a namespace that does not exist yet logs
  // an [E] line; this one just creates the (empty) namespace.
  if (!p.begin(kNvsNamespace, false)) return false;
  bool ok = false;
  if (p.isKey(kKeyFactory)) ok = p.getBytes(kKeyFactory, &out, sizeof(out)) == sizeof(out);
  p.end();
  return ok && gt911::checksumOf(out.bytes) == out.checksum;
}

bool saveBackup(const gt911::Config& c) {
  Backup b;
  memcpy(b.bytes, c.bytes, kConfigLen);
  b.checksum = c.checksum;
  Preferences p;
  if (!p.begin(kNvsNamespace, false)) return false;
  const size_t n = p.putBytes(kKeyFactory, &b, sizeof(b));
  p.end();
  return n == sizeof(b);
}

// A block good enough to be the base of a write: the chip answers, two reads agree, the
// stored checksum is right.
bool readClean(gt911::Config& out) {
  if (!gt911::ready()) {
    logLine("TOUCH the touch controller is not answering: nothing read, nothing written");
    return false;
  }
  if (!gt911::readConfig(out)) {
    logLine("TOUCH two reads of the configuration disagree, or I2C failed: nothing written. Try again");
    return false;
  }
  if (gt911::checksumOf(out.bytes) != out.checksum) {
    logLine("TOUCH the block in the chip has a wrong checksum (0x%02X stored, 0x%02X computed): not "
            "used as the base of a write", out.checksum, gt911::checksumOf(out.bytes));
    return false;
  }
  return true;
}

bool isYes(const char* word) { return word != nullptr && strcmp(word, "yes") == 0; }

// Splits up to `max` space-separated words of `rest` in place.
int words(char* rest, char* out[], int max) {
  int n = 0;
  char* p = rest;
  while (*p != '\0' && n < max) {
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    out[n++] = p;
    while (*p != '\0' && *p != ' ') ++p;
    if (*p == ' ') *p++ = '\0';
  }
  return n;
}

bool parseByte(const char* text, long lo, long hi, uint8_t& out) {
  if (text == nullptr || text[0] == '\0') return false;
  char* end = nullptr;
  const long v = strtol(text, &end, 10);
  if (end == text || *end != '\0' || v < lo || v > hi) return false;
  out = static_cast<uint8_t>(v);
  return true;
}

uint8_t percentOf(uint8_t value, uint8_t percent) {
  return static_cast<uint8_t>((static_cast<unsigned>(value) * percent + 50u) / 100u);
}

uint8_t maxOf(uint8_t a, uint8_t b) { return a > b ? a : b; }

// The factory values when they were saved, the chip's own otherwise.
const uint8_t* reference(const gt911::Config& cur, const Backup& backup, bool haveBackup) {
  return haveBackup ? backup.bytes : cur.bytes;
}

void logDiff(const uint8_t* from, const uint8_t* to, const char* tag) {
  char line[400];
  size_t used = 0;
  unsigned count = 0;
  for (size_t i = 0; i < kConfigLen; ++i) {
    if (from[i] == to[i]) continue;
    ++count;
    const int n = snprintf(line + used, sizeof(line) - used, "%s0x%04X %u->%u", used ? ", " : "",
                           static_cast<unsigned>(gt911::kConfigReg + i), from[i], to[i]);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(line) - used) break;
    used += static_cast<size_t>(n);
  }
  if (count == 0) logLine("%s no byte differs", tag);
  else logLine("%s %u byte(s) differ: %s", tag, count, line);
}

// Saves the factory copy if there is none yet, writes `next`, says how it went.
void apply(const gt911::Config& cur, const uint8_t* next, bool haveBackup, const char* what) {
  if (!haveBackup) {
    if (!saveBackup(cur)) {
      logLine("TOUCH could not save the factory copy in NVS: nothing written");
      return;
    }
    logLine("TOUCH factory block saved in NVS (namespace arrocco-gt911, key factory) before the first "
            "write; copy these lines somewhere safe too:");
    gt911::logConfig(cur, "TOUCH factory");
  }
  logLine("TOUCH writing %s - keep your hands off the glass for a second (the chip recalibrates)", what);
  gt911::Config after;
  if (gt911::writeConfig(next, after)) {
    logLine("TOUCH done: Screen_Touch_Level %u, Screen_Leave_Level %u, green mode after %u s, version %u,"
            " checksum 0x%02X. The chip keeps this after power-off; 'touch-restore yes' undoes it",
            after.bytes[gt911::kOffTouchLevel], after.bytes[gt911::kOffLeaveLevel],
            after.bytes[gt911::kOffLowPower] & 0x0Fu, after.bytes[gt911::kOffVersion], after.checksum);
    return;
  }
  logLine("TOUCH the chip did NOT end up with the new block. What it holds now:");
  gt911::Config now;
  if (gt911::readConfig(now)) {
    logDiff(next, now.bytes, "TOUCH compared with what was sent:");
    logLine("TOUCH Screen_Touch_Level %u, Screen_Leave_Level %u, checksum 0x%02X (computed 0x%02X)",
            now.bytes[gt911::kOffTouchLevel], now.bytes[gt911::kOffLeaveLevel], now.checksum,
            gt911::checksumOf(now.bytes));
  } else {
    logLine("TOUCH ... and it cannot be read back: 'touch-cfg' in a moment, 'touch-restore yes' if it "
            "looks wrong");
  }
}

void cmdShow() {
  if (!gt911::ready()) {
    logLine("TOUCH the touch controller is not answering: nothing to read");
    return;
  }
  gt911::Config cur;
  if (!gt911::readConfig(cur)) {
    logLine("TOUCH two reads of the configuration disagree, or I2C failed: try again");
    return;
  }
  gt911::logConfig(cur, "TOUCH cfg");
  Backup backup;
  if (!loadBackup(backup)) {
    logLine("TOUCH cfg no factory copy in NVS yet: the first write saves this block first");
  } else if (memcmp(backup.bytes, cur.bytes, kConfigLen) == 0) {
    logLine("TOUCH cfg identical to the factory copy kept in NVS");
  } else {
    logDiff(backup.bytes, cur.bytes, "TOUCH cfg compared with the factory copy:");
  }
}

void cmdLadder() {
  gt911::Config cur;
  if (!readClean(cur)) return;
  Backup backup;
  const bool haveBackup = loadBackup(backup);
  const uint8_t* ref = reference(cur, backup, haveBackup);
  const uint8_t touch0 = ref[gt911::kOffTouchLevel];
  const uint8_t leave0 = ref[gt911::kOffLeaveLevel];
  logLine("TOUCH ladder from the %s levels: touch %u, leave %u (the chip now: %u / %u). One step at a "
          "time; after each, try light taps everywhere and leave the board alone through a few refreshes",
          haveBackup ? "factory" : "current", touch0, leave0, cur.bytes[gt911::kOffTouchLevel],
          cur.bytes[gt911::kOffLeaveLevel]);
  uint8_t step = 0;
  for (const uint8_t percent : kLadderPercent) {
    const uint8_t touch = maxOf(kTouchFloor, percentOf(touch0, percent));
    uint8_t leave = maxOf(kLeaveFloor, percentOf(leave0, percent));
    if (leave >= touch) leave = static_cast<uint8_t>(touch - 1);
    if (touch >= touch0) continue; // the factory value is already at the floor
    logLine("TOUCH   step %u (%u%%): touch-level %u %u yes", ++step, percent, touch, leave);
  }
  if (step == 0) logLine("TOUCH   no step: the factory touch level is already at the floor (%u)", kTouchFloor);
  logLine("TOUCH ghost touches (TOUCH down lines with nobody touching) or taps from a finger above the "
          "glass mean one step too far: go back one, or 'touch-restore yes'");
}

void cmdLevel(char* rest) {
  char* w[4] = {};
  const int n = words(rest, w, 4);
  uint8_t touch = 0;
  uint8_t leave = 0;
  if (n < 2 || n > 3 || !parseByte(w[0], 1, 255, touch) || !parseByte(w[1], 1, 255, leave) ||
      (n == 3 && !isYes(w[2]))) {
    logLine("TOUCH usage: touch-level <touch> <leave> [yes] - 'touch-ladder' suggests the values");
    return;
  }
  const bool yes = (n == 3);
  gt911::Config cur;
  if (!readClean(cur)) return;
  Backup backup;
  const bool haveBackup = loadBackup(backup);
  const uint8_t* ref = reference(cur, backup, haveBackup);
  const uint8_t touch0 = ref[gt911::kOffTouchLevel];
  const uint8_t leave0 = ref[gt911::kOffLeaveLevel];
  // Only the way down is limited: a higher threshold is always safe (fewer ghost touches),
  // and it is how the factory values come back by hand if the NVS copy is ever lost.
  const uint8_t touchMin = maxOf(kTouchFloor, static_cast<uint8_t>(touch0 / 2));
  const uint8_t leaveMin = maxOf(kLeaveFloor, static_cast<uint8_t>(leave0 / 2));
  if (touch < touchMin) {
    logLine("TOUCH refused: touch level %u is below %u (half of the %s %u)", touch, touchMin,
            haveBackup ? "factory" : "current", touch0);
    return;
  }
  if (leave < leaveMin || leave >= touch) {
    logLine("TOUCH refused: leave level %u must be at least %u (half of the %s %u) and below the touch "
            "level %u", leave, leaveMin, haveBackup ? "factory" : "current", leave0, touch);
    return;
  }
  uint8_t next[kConfigLen];
  memcpy(next, cur.bytes, kConfigLen);
  next[gt911::kOffTouchLevel] = touch;
  next[gt911::kOffLeaveLevel] = leave;
  if (memcmp(next, cur.bytes, kConfigLen) == 0) {
    logLine("TOUCH the chip already has touch %u, leave %u: nothing to write", touch, leave);
    return;
  }
  if (!yes) {
    logLine("TOUCH would write Screen_Touch_Level %u -> %u, Screen_Leave_Level %u -> %u (version stays %u,"
            " checksum 0x%02X -> 0x%02X)%s. The chip keeps it after power-off. To write it, type: "
            "touch-level %u %u yes",
            cur.bytes[gt911::kOffTouchLevel], touch, cur.bytes[gt911::kOffLeaveLevel], leave,
            cur.bytes[gt911::kOffVersion], cur.checksum, gt911::checksumOf(next),
            haveBackup ? "" : ", after saving the factory block in NVS", touch, leave);
    return;
  }
  char what[64];
  snprintf(what, sizeof(what), "touch level %u, leave level %u", touch, leave);
  apply(cur, next, haveBackup, what);
}

void cmdGreen(char* rest) {
  char* w[3] = {};
  const int n = words(rest, w, 3);
  uint8_t seconds = 0;
  if (n < 1 || n > 2 || !parseByte(w[0], 1, 15, seconds) || (n == 2 && !isYes(w[1]))) {
    logLine("TOUCH usage: touch-green <seconds 1..15> [yes] - how long the chip stays in normal mode "
            "(a scan every report period) before green mode (a scan every ~40 ms)");
    return;
  }
  const bool yes = (n == 2);
  gt911::Config cur;
  if (!readClean(cur)) return;
  Backup backup;
  const bool haveBackup = loadBackup(backup);
  const uint8_t factory = reference(cur, backup, haveBackup)[gt911::kOffLowPower] & 0x0Fu;
  if (seconds < factory)
    logLine("TOUCH note: %u s is shorter than the %s %u s - the first tap after a pause gets slower, not "
            "faster", seconds, haveBackup ? "factory" : "current", factory);
  uint8_t next[kConfigLen];
  memcpy(next, cur.bytes, kConfigLen);
  next[gt911::kOffLowPower] = static_cast<uint8_t>((cur.bytes[gt911::kOffLowPower] & 0xF0u) | seconds);
  if (memcmp(next, cur.bytes, kConfigLen) == 0) {
    logLine("TOUCH the chip already switches to green mode after %u s: nothing to write", seconds);
    return;
  }
  if (!yes) {
    logLine("TOUCH would write Low_Power_Control %u s -> %u s (version stays %u). The chip keeps it after "
            "power-off. To write it, type: touch-green %u yes",
            cur.bytes[gt911::kOffLowPower] & 0x0Fu, seconds, cur.bytes[gt911::kOffVersion], seconds);
    return;
  }
  char what[48];
  snprintf(what, sizeof(what), "green mode after %u s", seconds);
  apply(cur, next, haveBackup, what);
}

void cmdRestore(char* rest) {
  char* w[2] = {};
  const int n = words(rest, w, 2);
  if (n > 1 || (n == 1 && !isYes(w[0]))) {
    logLine("TOUCH usage: touch-restore [yes]");
    return;
  }
  Backup backup;
  if (!loadBackup(backup)) {
    logLine("TOUCH no factory copy in NVS: nothing was ever written by this firmware, nothing to restore");
    return;
  }
  gt911::Config cur;
  if (!gt911::ready() || !gt911::readConfig(cur)) {
    logLine("TOUCH the configuration cannot be read right now: try again");
    return;
  }
  if (memcmp(cur.bytes, backup.bytes, kConfigLen) == 0 && cur.checksum == backup.checksum) {
    logLine("TOUCH the chip already holds the factory block: nothing to write");
    return;
  }
  if (n == 0) {
    logDiff(cur.bytes, backup.bytes, "TOUCH restore would change:");
    logLine("TOUCH to write the factory block back, type: touch-restore yes");
    return;
  }
  // The version byte is the factory one too: equal to the chip's own unless something else
  // raised it, in which case the chip refuses a lower one and says so in the read-back.
  apply(cur, backup.bytes, true, "the factory block back");
}

} // namespace

bool consoleCommand(const char* verb, char* rest) {
  if (strcmp(verb, "touch-cfg") == 0) cmdShow();
  else if (strcmp(verb, "touch-ladder") == 0) cmdLadder();
  else if (strcmp(verb, "touch-level") == 0) cmdLevel(rest);
  else if (strcmp(verb, "touch-green") == 0) cmdGreen(rest);
  else if (strcmp(verb, "touch-restore") == 0) cmdRestore(rest);
  else return false;
  return true;
}

void consoleHelp() {
  logLine("CMD   touch-cfg | touch-ladder | touch-level <touch> <leave> [yes] | touch-green <s> [yes] | "
          "touch-restore [yes]");
}

} // namespace touch_tune
