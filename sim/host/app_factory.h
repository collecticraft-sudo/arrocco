// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — the ONE place that decides which arrocco::App the simulator
// runs. main.cpp knows nothing else about the application.
//
// To run the real app instead of the demo, change only app_factory.cpp: include the
// core's header and return the real App. Its sources are already compiled and linked,
// because sim/Makefile builds every .cpp under lib/arrocco/src.
#pragma once

#include <arrocco/platform.h>

#include <string>

namespace arrocco_sim {

class FakeLichess;

// How the app is put together, from the command line.
struct AppOptions {
  // Lichess against the pretend one of fake_lichess.h: no network, no account. Without it the
  // Lichess screens use the proxy in sim/server.py when one is there (ARROCCO_SIM_PROXY, which
  // server.py sets for its child), and the menu entry stays greyed when there is none.
  bool fakeLichess = false;
};

// Returns the application bound to this platform. The object is owned by the factory
// and lives until the process exits: never delete it. Called exactly once.
arrocco::App* createApp(arrocco::Platform& platform, const AppOptions& options);

// The pretend Lichess, for the protocol's "lichess ..." lines; nullptr without --fake-lichess.
FakeLichess* fakeLichess();

// What the app shows, for "lichess state": ,"screen":"lichess","page":"hub" (to be put inside a
// JSON object; "page" is the Lichess page, or the online game's mode).
std::string screenFields();

// Short identifier shown in the simulator's control strip ("demo", "arrocco", ...).
const char* appName();

// Call after createApp() and before App::begin(): this start is the wake from a deep sleep
// that began with a board on the glass (what power::wakeIntoGame() says on the device),
// so the app goes back to that board instead of its menu. arrocco-sim --wake.
void prepareWake();

}  // namespace arrocco_sim
