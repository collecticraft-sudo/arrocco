// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — which App runs: the real Arrocco UI, with the CT800 engine
// behind it. See app_factory.h.
// To go back to the placeholder, return demoApp(platform) from demo_app.h instead.
#include "app_factory.h"

#include <arrocco/ui/app.h>
#include <arrocco/ui/lichess_state.h>

#include <cstdlib>

#include "fake_lichess.h"
#include "host_engine.h"
#include "lichess_transport.h"
#include "sim_account.h"

namespace arrocco_sim {

namespace {

FakeLichess* g_fake = nullptr;
arrocco::Platform* g_platform = nullptr;
arrocco::ui::ChessApp* g_app = nullptr;

// The proxy measures its timeouts on the simulator's clock, virtual time included.
uint32_t platformMillis() { return g_platform->millis(); }

}  // namespace

arrocco::App* createApp(arrocco::Platform& platform, const AppOptions& options) {
  static arrocco::ui::ChessApp app(platform, hostEngine());
  g_platform = &platform;
  g_app = &app;
  if (options.fakeLichess) {
    static FakeLichess fake(platform);
    static arrocco::ui::LichessState state(fake, fake);
    g_fake = &fake;
    app.setLichess(&state);
  } else if (const char* proxy = std::getenv("ARROCCO_SIM_PROXY"); proxy != nullptr && proxy[0] != '\0') {
    ProxyTransport& transport = lichessTransport();
    transport.setClock(&platformMillis);
    static ProxyAccount account(transport);
    static arrocco::ui::LichessState state(transport, account);
    app.setLichess(&state);
  }
  return &app;
}

FakeLichess* fakeLichess() { return g_fake; }

std::string screenFields() {
  using arrocco::ui::ScreenId;
  if (g_app == nullptr) return "";
  const char* screen = "menu";
  std::string page;
  switch (g_app->currentScreen()) {
    case ScreenId::Menu:        screen = "menu"; break;
    case ScreenId::ClockPicker: screen = "clock"; break;
    case ScreenId::Settings:    screen = "settings"; break;
    case ScreenId::EngineSetup: screen = "engine"; break;
    case ScreenId::Game:        screen = "game"; break;
    case ScreenId::GameOver:    screen = "over"; break;
    case ScreenId::Lichess:
      screen = "lichess";
      page = arrocco::ui::LichessScreen::pageName(g_app->lichessScreen().page());
      break;
    case ScreenId::LichessGame:
      screen = "lichess-game";
      page = g_app->lichessGameScreen().modeName();
      break;
  }
  return std::string(",\"screen\":\"") + screen + "\",\"page\":\"" + page + "\"";
}

const char* appName() { return "arrocco"; }

}  // namespace arrocco_sim
