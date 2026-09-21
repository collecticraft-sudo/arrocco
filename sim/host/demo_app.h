// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — a stand-in App, written only against arrocco/platform.h and
// Adafruit_GFX exactly as the real one will be. It exists so the simulator can be
// tried before the rules library and the real UI are there. No chess rules inside.
#pragma once

#include <arrocco/platform.h>

namespace arrocco_sim {

// The single demo instance (static storage, no heap). Owned by this module.
arrocco::App* demoApp(arrocco::Platform& platform);

}  // namespace arrocco_sim
