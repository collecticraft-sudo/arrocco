// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — which App runs. Today: the demo. See app_factory.h.
#include "app_factory.h"

#include "demo_app.h"

namespace arrocco_sim {

arrocco::App* createApp(arrocco::Platform& platform) {
  // Next phase, for example:
  //   #include <arrocco/ui/app.h>
  //   static arrocco::ui::ChessApp app(platform);
  //   return &app;
  return demoApp(platform);
}

const char* appName() { return "demo"; }

}  // namespace arrocco_sim
