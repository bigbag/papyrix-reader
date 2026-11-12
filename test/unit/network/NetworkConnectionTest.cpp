#include <Arduino.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <WiFi.h>
#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>
#include <builtinFonts/ui_12.h>
#include <builtinFonts/ui_bold_12.h>

#include <cassert>
#include <filesystem>
#include <fstream>

#include "../settings/MurphyM4SettingsFixture.h"
#include "network/PapyrixWebServer.h"
#include "network/WifiCredentialStore.h"
#include "states/NetworkState.h"
#include "test_utils.h"

namespace {
int serverStarts = 0;
}

namespace papyrix {
PapyrixWebServer::PapyrixWebServer() = default;
PapyrixWebServer::~PapyrixWebServer() = default;
void PapyrixWebServer::begin() {
  ++serverStarts;
  running_ = true;
}
void PapyrixWebServer::stop() { running_ = false; }
void PapyrixWebServer::handleClient() {}
}  // namespace papyrix

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("Network connection cancellation and handoff");
  auto& core = papyrix::core;
  auto& store = papyrix::WIFI_STORE;
  auto prepare = [&](papyrix::SyncMode purpose) {
    core.wifi.shutdown();
    WiFi.reset();
    SdMan.reset();
    store.clearAll();
    assert(store.addCredential("First", "first-password"));
    assert(store.addCredential("Second", "second-password"));
    core.pendingSync = purpose;
    core.pendingAppId = -1;
    testSetManualMillis(0);
    serverStarts = 0;
  };
  GfxRenderer renderer(core.display);
  renderer.begin();
  EpdFont regular(&ui_12), bold(&ui_bold_12);
  EpdFont readerRegular(&reader_2b), readerBold(&reader_bold_2b);
  renderer.insertFont(UI_FONT_ID, EpdFontFamily(&regular, &bold));
  renderer.insertFont(READER_FONT_ID, EpdFontFamily(&readerRegular, &readerBold));
  const std::filesystem::path renderDir = argc == 3 && std::string(argv[1]) == "--render-dir" ? argv[2] : "";
  auto capture = [&](papyrix::NetworkState& state, const char* name) {
    state.render(core);
    if (renderDir.empty()) return;
    std::filesystem::create_directories(renderDir);
    std::ofstream file(renderDir / name);
    file << "P1\n" << renderer.getScreenWidth() << ' ' << renderer.getScreenHeight() << '\n';
    for (int y = 0; y < renderer.getScreenHeight(); ++y) {
      for (int x = 0; x < renderer.getScreenWidth(); ++x) {
        papyrix::board::PanelPoint point{};
        assert(papyrix::board::panelFromLogical(
            static_cast<papyrix::board::DisplayOrientation>(renderer.getOrientation()), 800, 480, x, y, point));
        file << ((core.display.getFrameBuffer()[point.y * 100 + point.x / 8] & (0x80 >> (point.x % 8))) ? 0 : 1) << ' ';
      }
      file << '\n';
    }
    assert(file.good());
  };
  auto press = [&](papyrix::NetworkState& state, papyrix::Button button) {
    assert(core.events.push(papyrix::Event::buttonPress(button)));
    return state.update(core);
  };
  using papyrix::Button;
  using papyrix::StateId;
  using papyrix::SyncMode;

  {
    prepare(SyncMode::WifiSetup);
    WiFi.connectDelayMs = 60000;
    papyrix::NetworkState state(renderer);
    state.enter(core);
    press(state, Button::Center);
    press(state, Button::Center);
    const auto started = millis();
    press(state, Button::Center);
    runner.expectTrue(millis() - started < 1000 && WiFi.attemptActive,
                      "saved connection starts without blocking the UI for its deadline");
    capture(state, "connecting.pbm");
    runner.expectTrue(press(state, Button::Back).next == StateId::Network && WiFi.getMode() == WIFI_OFF,
                      "Cancel stops the pending radio attempt without leaving Settings WiFi");
    delay(60000);
    state.update(core);
    runner.expectFalse(core.wifi.isConnected() || WiFi.status() == WL_CONNECTED,
                       "cancelled UI connection cannot complete later");
    press(state, Button::Center);
    runner.expectTrue(WiFi.attemptActive && WiFi.attemptedSsids.size() == 2,
                      "Cancel returns to the saved network action so Connect can start again");
    state.exit(core);
  }
  {
    prepare(SyncMode::FileTransfer);
    WiFi.connectDelayMs = 60000;
    papyrix::NetworkState state(renderer);
    state.enter(core);
    const auto started = millis();
    press(state, Button::Center);
    runner.expectTrue(millis() - started < 1000 && WiFi.attemptedSsids == std::vector<std::string>{"First"},
                      "Recent starts one credential without blocking or trying the next network");
    runner.expectTrue(press(state, Button::Back).next == StateId::Network && WiFi.getMode() == WIFI_OFF,
                      "Recent Cancel returns to network mode selection and stops the radio");
    press(state, Button::Center);
    runner.expectTrue(WiFi.attemptedSsids == std::vector<std::string>{"First", "First"},
                      "Recent can restart from its first credential after cancellation");
    state.exit(core);
  }
  {
    prepare(SyncMode::CalibreWireless);
    WiFi.connectDelayMs = 60000;
    papyrix::NetworkState state(renderer);
    state.enter(core);
    press(state, Button::Center);
    delay(15000);
    state.update(core);
    runner.expectTrue(WiFi.attemptedSsids == std::vector<std::string>{"First", "Second"},
                      "Recent advances to the next credential after the first deadline");
    WiFi.connectDelayMs = 0;
    runner.expectTrue(state.update(core).next == StateId::CalibreSync && core.wifi.isConnected(),
                      "successful Recent connection hands the live station to Calibre");
    runner.expectTrue(std::string(store.getCredentials()[0].ssid) == "Second",
                      "successful fallback becomes the first saved network");
    state.exit(core);
    runner.expectTrue(core.wifi.isConnected(), "Calibre handoff does not stop the station");
    core.wifi.shutdown();
  }
  {
    prepare(SyncMode::FileTransfer);
    WiFi.failBegin = true;
    papyrix::NetworkState state(renderer);
    state.enter(core);
    press(state, Button::Center);
    state.update(core);
    runner.expectTrue(WiFi.attemptedSsids == std::vector<std::string>{"First", "Second"} && WiFi.getMode() == WIFI_OFF,
                      "Recent exhausts immediate failures without leaving a station attempt running");
    press(state, Button::Center);
    runner.expectEq(size_t{2}, WiFi.attemptedSsids.size(), "all failures select Join instead of restarting Recent");
    state.exit(core);
  }
  {
    prepare(SyncMode::WifiSetup);
    papyrix::NetworkState state(renderer);
    state.enter(core);
    press(state, Button::Center);
    press(state, Button::Center);
    press(state, Button::Center);
    runner.expectTrue(core.wifi.isConnected() && serverStarts == 0,
                      "manual success waits for Done before opening the web server");
    capture(state, "connected.pbm");
    press(state, Button::Center);
    runner.expectEq(1, serverStarts, "Done starts the web server after a manual connection");
    state.exit(core);
    runner.expectEq(WIFI_OFF, WiFi.getMode(), "leaving WiFi setup releases the radio");
  }
  for (const auto purpose : {SyncMode::CalibreWireless, SyncMode::PrinterSetup}) {
    prepare(purpose);
    WiFi.scanCount = 1;
    WiFi.scannedSsid = "First";
    papyrix::NetworkState state(renderer);
    state.enter(core);
    press(state, Button::Down);
    press(state, Button::Center);
    press(state, Button::Center);
    assert(core.wifi.isConnected());

    assert(core.events.push(papyrix::Event::buttonPress(Button::Center)));
    assert(core.events.push(papyrix::Event::buttonPress(Button::Back)));
    const auto next = state.update(core).next;
    const auto destination = purpose == SyncMode::CalibreWireless ? StateId::CalibreSync : StateId::AppLauncher;
    runner.expectTrue(next == destination, "Done commits the selected WiFi handoff before queued Back");
    runner.expectTrue(core.events.empty(), "queued input cannot reach the destination state");
    state.exit(core);
    runner.expectTrue(core.wifi.isConnected() && WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED,
                      "the destination inherits a live station after queued Done and Back");
    core.wifi.shutdown();
  }
  testManualMillisEnabled = false;
  return runner.allPassed() ? 0 : 1;
}
