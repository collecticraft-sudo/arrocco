// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — which App runs: the real Arrocco UI, with the CT800 engine
// behind it. See app_factory.h.
// To go back to the placeholder, return demoApp(platform) from demo_app.h instead.
#include "app_factory.h"

#include <arrocco/ui/app.h>

#include "host_engine.h"

namespace arrocco_sim {

namespace {
arrocco::ui::ChessApp* g_app = nullptr;
}  // namespace

arrocco::App* createApp(arrocco::Platform& platform) {
  static arrocco::ui::ChessApp app(platform, hostEngine());
  g_app = &app;
  return &app;
}

void prepareWake() {
  if (g_app != nullptr) g_app->wakeIntoGame();
}

const char* appName() { return "arrocco"; }

}  // namespace arrocco_sim
