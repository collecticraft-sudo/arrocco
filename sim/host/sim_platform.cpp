// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — SimPlatform implementation. See sim_platform.h.
#include "sim_platform.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace arrocco_sim {

namespace {

uint32_t nominalRefreshMs(arrocco::Refresh kind) {
  switch (kind) {
    case arrocco::Refresh::Partial: return kPartialMs;
    case arrocco::Refresh::Full:    return kFullMs;
    case arrocco::Refresh::Deep:    return kDeepMs;
  }
  return kPartialMs;
}

// The keys Platform::loadBlob / storeBlob accept: what NVS takes as a key, and nothing
// that could leave the state directory as a file name.
bool validKey(const char* key) {
  if (key == nullptr || key[0] == '\0') return false;
  for (size_t n = 0; key[n] != '\0'; ++n) {
    const char c = key[n];
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!allowed || n >= 15) return false;
  }
  return true;
}

}  // namespace

SimPlatform::SimPlatform(Emitter& out, bool virtualTime)
    : out_(out),
      virtualTime_(virtualTime),
      canvas_(static_cast<uint16_t>(arrocco::kScreenW), static_cast<uint16_t>(arrocco::kScreenH)),
      start_(Clock::now()),
      busyUntil_(start_) {
  // GFXcanvas1 starts all zero bits, which is all black. Start from blank paper
  // instead; the app is expected to paint every pixel in begin() anyway.
  canvas_.fillScreen(arrocco::kWhite);
  memset(presented_, 0xFF, sizeof presented_);
}

uint32_t SimPlatform::millisAt(Clock::time_point when) const {
  if (virtualTime_) return virtualMs_;
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(when - start_);
  // Wraps after 49.7 days exactly like the uint32_t millis() on the device.
  return static_cast<uint32_t>(elapsed.count());
}

uint32_t SimPlatform::millis() { return millisAt(Clock::now()); }

void SimPlatform::present(arrocco::Refresh kind) {
  const bool powerOn = !panelOn_;
  const uint32_t nominal = nominalRefreshMs(kind) + (powerOn ? kPowerOnMs : 0);
  const uint32_t block = static_cast<uint32_t>(lround(nominal * latencyScale_));

  switch (kind) {
    case arrocco::Refresh::Partial: ++counts_.partial; ++sinceFull_; break;
    case arrocco::Refresh::Full:    ++counts_.full; sinceFull_ = 0; break;
    case arrocco::Refresh::Deep:    ++counts_.deep; sinceFull_ = 0; break;
  }

  memcpy(presented_, canvas_.getBuffer(), sizeof presented_);
  lastFrame_.seq = ++seq_;
  lastFrame_.kind = kind;
  lastFrame_.timeMs = millis();
  lastFrame_.nominalMs = nominal;
  lastFrame_.blockMs = block;
  lastFrame_.powerOn = powerOn;
  lastFrame_.resend = false;
  lastFrame_.counts = counts_;
  lastFrame_.sinceFull = sinceFull_;
  anyFrame_ = true;
  out_.frame(lastFrame_, presented_);

  if (powerOn) {
    panelOn_ = true;
    out_.panel(true, lastFrame_.timeMs);
  }

  if (virtualTime_) {
    // The core must see the clock jump by the real refresh time, scale or not:
    // that is what its timeouts and chess clocks will live with on the device.
    virtualMs_ += nominal;
  } else {
    blockFor(block);
  }
  busyUntil_ = Clock::now();
}

void SimPlatform::blockFor(uint32_t ms) {
  if (ms == 0) return;
  std::unique_lock<std::mutex> lock(stopMutex_);
  stopSignal_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return stopRequested_; });
}

void SimPlatform::requestStop() {
  {
    std::lock_guard<std::mutex> lock(stopMutex_);
    stopRequested_ = true;
  }
  stopSignal_.notify_all();
}

void SimPlatform::panelOff() {
  if (!panelOn_) return;
  panelOn_ = false;
  out_.panel(false, millis());
}

void SimPlatform::beep(uint16_t hz, uint16_t ms) {
  // tone() on the device does not block either: the buzzer plays while the code goes on.
  out_.beep(hz, ms, millis());
}

// ---- the board's flash: one file per key --------------------------------------------

bool SimPlatform::setStateDir(const char* dir) {
  stateDir_[0] = '\0';
  if (dir == nullptr || dir[0] == '\0' || strlen(dir) >= sizeof stateDir_) return false;
  if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false;
  struct stat info;
  if (stat(dir, &info) != 0 || !S_ISDIR(info.st_mode)) return false;
  snprintf(stateDir_, sizeof stateDir_, "%s", dir);
  return true;
}

bool SimPlatform::blobPath(const char* key, const char* suffix, char* out, size_t outSize) const {
  if (stateDir_[0] == '\0' || !validKey(key)) return false;
  const int n = snprintf(out, outSize, "%s/%s%s", stateDir_, key, suffix);
  return n > 0 && static_cast<size_t>(n) < outSize;
}

size_t SimPlatform::loadBlob(const char* key, uint8_t* out, size_t capacity) {
  char path[sizeof stateDir_ + 32];
  if (out == nullptr || capacity == 0 || !blobPath(key, ".bin", path, sizeof path)) return 0;
  FILE* file = fopen(path, "rb");
  if (file == nullptr) return 0;
  const size_t got = fread(out, 1, capacity, file);
  // A blob larger than `capacity` is no blob at all, as the interface says.
  const bool tooLarge = got == capacity && fgetc(file) != EOF;
  const bool failed = ferror(file) != 0;
  fclose(file);
  return (tooLarge || failed) ? 0 : got;
}

bool SimPlatform::storeBlob(const char* key, const uint8_t* data, size_t size) {
  char path[sizeof stateDir_ + 32];
  char temp[sizeof stateDir_ + 32];
  if (!blobPath(key, ".bin", path, sizeof path) || !blobPath(key, ".tmp", temp, sizeof temp)) return false;
  // Written beside the old file, then renamed over it: rename() is atomic, so a process
  // killed at any moment leaves the old blob or the new one, as NVS does after a power cut.
  bool ok = data != nullptr && size > 0;
  FILE* file = ok ? fopen(temp, "wb") : nullptr;
  if (file != nullptr) {
    ok = fwrite(data, 1, size, file) == size;
    ok = fclose(file) == 0 && ok;
    ok = ok && rename(temp, path) == 0;
    if (!ok) remove(temp);
  } else {
    ok = false;
  }
  out_.store(key, size, ok, millis());
  return ok;
}

void SimPlatform::setBatteryPercent(int percent) {
  if (percent < -1) percent = -1;
  if (percent > 100) percent = 100;
  batteryPercent_ = percent;
}

void SimPlatform::setLatencyScale(double scale) {
  if (!(scale >= 0.0)) scale = 0.0;   // also catches NaN
  if (scale > 4.0) scale = 4.0;
  latencyScale_ = scale;
}

void SimPlatform::advanceVirtualTime(uint32_t ms) {
  if (virtualTime_) virtualMs_ += ms;
}

void SimPlatform::resendLastFrame() {
  if (!anyFrame_) return;
  FrameInfo again = lastFrame_;
  again.resend = true;
  out_.frame(again, presented_);
}

void SimPlatform::reportState() { out_.state(batteryPercent_, usbPowered_, latencyScale_); }

}  // namespace arrocco_sim
