// SPDX-License-Identifier: GPL-3.0-or-later
// GT911 touch controller: read-only driver (the factory config block is never written).
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

// Discards what was reported while the screen was refreshing, then listens for a
// short while. Returns true when a finger is still on the glass.
bool flush();

uint32_t frames();   // fresh reports seen by polling
uint32_t intEdges(); // edges counted on the INT wire by the ISR
enum class WireFault : uint8_t { None, IntMissing, RstMissing };
WireFault wireFault(); // named once a few touches have been polled

} // namespace gt911
