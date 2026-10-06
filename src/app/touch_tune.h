// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware - the GT911 configuration on the serial console.
//
// "devo premere forte": a light touch does not register. The threshold that decides it
// lives in the GT911's own configuration (Screen_Touch_Level / Screen_Leave_Level), which
// the panel maker wrote at the factory. These commands show it, and lower it ONLY when
// the command is typed a second time with "yes" at the end:
//
//   touch-cfg                         the whole block, decoded, with its checksum
//   touch-ladder                      the values worth trying, from the factory ones
//   touch-level <touch> <leave> [yes] a lower Screen_Touch_Level / Screen_Leave_Level
//   touch-green <seconds> [yes]       later switch to green mode (slower scanning)
//   touch-restore [yes]               the factory block back
//
// The chip stores what it is sent in its own flash: a write survives power-off. So
// before the first write the block found in the chip is saved in NVS (namespace
// "arrocco-gt911", key "factory") and printed in hex, and touch-restore writes it back.
// Nothing here runs unless typed; the firmware never writes the configuration itself.
#pragma once

namespace touch_tune {

bool consoleCommand(const char* verb, char* rest);
void consoleHelp();

} // namespace touch_tune
