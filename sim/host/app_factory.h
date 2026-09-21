// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — the ONE place that decides which arrocco::App the simulator
// runs. main.cpp knows nothing else about the application.
//
// To run the real app instead of the demo, change only app_factory.cpp: include the
// core's header and return the real App. Its sources are already compiled and linked,
// because sim/Makefile builds every .cpp under lib/arrocco/src.
#pragma once

#include <arrocco/platform.h>

namespace arrocco_sim {

// Returns the application bound to this platform. The object is owned by the factory
// and lives until the process exits: never delete it. Called exactly once.
arrocco::App* createApp(arrocco::Platform& platform);

// Short identifier shown in the simulator's control strip ("demo", "arrocco", ...).
const char* appName();

}  // namespace arrocco_sim
