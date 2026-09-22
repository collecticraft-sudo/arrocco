// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — the arrocco::Engine the Mac build runs: CT800 on a std::thread.
//
// The simulator may run on virtual time (millis() only moves when the script says
// "tick"), but the ENGINE always runs on the real clock: it is a real search doing
// real work, and pretending otherwise would hide how long a move takes. A scripted
// session therefore keeps ticking until thinking() goes false, exactly as the
// firmware's loop() does.
#pragma once

#include <arrocco/engine.h>

namespace arrocco_sim {

// The one engine of this process, already started, or nullptr if its hash memory
// could not be set up. Owned by the factory: never delete it.
arrocco::Engine* hostEngine();

}  // namespace arrocco_sim
