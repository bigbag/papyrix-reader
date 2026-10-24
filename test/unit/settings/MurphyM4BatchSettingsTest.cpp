#include <Arduino.h>
#include <GfxRenderer.h>
#include <MurphyM4Batch.h>
#include <Preferences.h>
#include <TargetConfig.h>
#include <SDCardManager.h>
#include <builtinFonts/ui_12.h>
#include <builtinFonts/ui_bold_12.h>
#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>

#include <cassert>
#include <filesystem>
#include <fstream>

#include "core/Core.h"
#include "states/SettingsState.h"
#include "ThemeManager.h"
#include "test_utils.h"
#include "MurphyM4SettingsFixture.h"

namespace {
GfxRenderer* activeRenderer = nullptr;
std::filesystem::path captureNext;
bool savedBeforeRestart = false;
papyrix::Core* activeCore = nullptr;
papyrix::Settings settingsAtRestart;
bool settingsLoadedBeforeRestart = false;
void capture(const papyrix::hal::Display& display) {
  if (captureNext.empty()) return;
  auto& renderer = *activeRenderer;
  std::filesystem::create_directories(captureNext.parent_path());
  std::ofstream out(captureNext);
  out << "P1\n" << renderer.getScreenWidth() << ' ' << renderer.getScreenHeight() << '\n';
  for (int y = 0; y < renderer.getScreenHeight(); ++y) {
    for (int x = 0; x < renderer.getScreenWidth(); ++x) {
      papyrix::board::PanelPoint panel{};
      assert(papyrix::board::panelFromLogical(static_cast<papyrix::board::DisplayOrientation>(renderer.getOrientation()),
                                            800, 480, x, y, panel));
      out << ((display.getFrameBuffer()[panel.y * 100 + panel.x / 8] & (0x80 >> (panel.x % 8))) ? 0 : 1) << ' ';
    }
    out << '\n';
  }
  assert(out.good());
  captureNext.clear();
}
void beforeRestart() {
  Preferences store;
  assert(store.begin("papyrix_hw", true));
  savedBeforeRestart = store.getUChar("m4_batch", 0) == 1;
  store.end();
  settingsLoadedBeforeRestart = settingsAtRestart.load(activeCore->storage).ok();
}
}

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("M4 settings transactions and real font layout");
  const std::filesystem::path renderDir = argc == 3 && std::string(argv[1]) == "--render-dir" ? argv[2] : "";
  EpdFont uiRegular(&ui_12), uiBold(&ui_bold_12), readerRegular(&reader_2b), readerBold(&reader_bold_2b);
  for (int orientation = 0; orientation < 4; ++orientation) {
    Preferences::failBegin = Preferences::failPut = false;
    Preferences store;
    store.begin("papyrix_hw");
    store.remove("m4_batch");
    store.end();
    Preferences::putCalls = 0;
    ESP.restartCount = 0;
    ESP.beforeRestart = beforeRestart;
    savedBeforeRestart = false;
    settingsLoadedBeforeRestart = false;
    SdMan.reset();
#if PAPYRIX_TARGET_MURPHY_M4
    papyrix::board::loadMurphyM4Batch();
#endif
    papyrix::Core core;
    activeCore = &core;
    assert(core.storage.init().ok());
    assert(core.settings.save(core.storage).ok());
    GfxRenderer renderer(core.display);
    activeRenderer = &renderer;
    renderer.begin();
    renderer.insertFont(UI_FONT_ID, EpdFontFamily(&uiRegular, &uiBold));
    renderer.insertFont(READER_FONT_ID, EpdFontFamily(&readerRegular, &readerBold));
    renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
    papyrix::hal::Display::onFlush = capture;
    papyrix::SettingsState state(renderer);
    state.enter(core);
    auto send = [&](papyrix::Event event) {
      assert(core.events.push(event));
      runner.expectTrue(state.update(core).next == papyrix::StateId::Settings, "batch input stays in Settings");
    };
    auto press = [&](papyrix::Button button) { send(papyrix::Event::buttonPress(button)); };
    auto row = [&](int index) {
      send(papyrix::Event::tap({100, static_cast<int16_t>(60 + index * (THEME.itemHeight + THEME.itemSpacing) +
                                                       THEME.itemHeight / 2)}));
    };
    row(2);
#if PAPYRIX_TARGET_MURPHY_M4
    const auto originalFront = core.settings.frontButtonLayout;
    const auto originalSide = core.settings.sideButtonLayout;
    auto editLayouts = [&] {
      row(1);
      press(papyrix::Button::Right);
      row(2);
      press(papyrix::Button::Right);
    };
    editLayouts();
#endif
    row(8);
    if (!renderDir.empty()) captureNext = renderDir / std::to_string(orientation) / "device.pbm";
    state.render(core);
#if PAPYRIX_TARGET_MURPHY_M4
    press(papyrix::Button::Center);
    runner.expectTrue(Preferences::putCalls == 0 && ESP.restartCount == 0, "unchanged batch does not save or restart");
    press(papyrix::Button::Right);
    if (!renderDir.empty()) captureNext = renderDir / std::to_string(orientation) / "confirm.pbm";
    state.render(core);
    press(papyrix::Button::Center);
    runner.expectTrue(Preferences::putCalls == 0 && ESP.restartCount == 0, "default No cancels the changed batch");
    press(papyrix::Button::Back);
    runner.expectTrue(core.settings.frontButtonLayout != originalFront &&
                          core.settings.sideButtonLayout != originalSide,
                      "No cancellation preserves unrelated pending button layouts");
    row(2);
    editLayouts();
    row(8);
    press(papyrix::Button::Right);
    press(papyrix::Button::Back);
    runner.expectTrue(Preferences::putCalls == 0 && ESP.restartCount == 0, "Back cancels without changing NVS");
    press(papyrix::Button::Back);
    runner.expectTrue(core.settings.frontButtonLayout == originalFront &&
                          core.settings.sideButtonLayout == originalSide,
                      "Back cancellation preserves unrelated pending button layouts");
    row(2);
    editLayouts();
    row(8);
    press(papyrix::Button::Right);
    press(papyrix::Button::Up);
    Preferences::failPut = true;
    if (!renderDir.empty()) captureNext = renderDir / std::to_string(orientation) / "save-error.pbm";
    press(papyrix::Button::Center);
    runner.expectTrue(ESP.restartCount == 0 && papyrix::board::murphyM4Batch() == papyrix::board::MurphyM4Batch::Second,
                      "failed save retains active batch without restart");
    if (!renderDir.empty()) assert(captureNext.empty());
    Preferences::failPut = false;
    press(papyrix::Button::Back);
    runner.expectTrue(core.settings.frontButtonLayout != originalFront &&
                          core.settings.sideButtonLayout != originalSide,
                      "batch save failure preserves unrelated pending button layouts");
    row(2);
    editLayouts();
    row(6);
    press(papyrix::Button::Right);
    const auto expectedSleep = core.settings.autoSleepMinutes;
    row(8);
    const auto putsBeforeSdFailure = Preferences::putCalls;
    SdMan.setOpenFileForWriteFailCount(1);
    press(papyrix::Button::Right);
    press(papyrix::Button::Up);
    press(papyrix::Button::Center);
    runner.expectTrue(ESP.restartCount == 0 && Preferences::putCalls == putsBeforeSdFailure,
                      "settings storage failure blocks batch commit and restart");
    runner.expectTrue(core.settings.frontButtonLayout != originalFront &&
                          core.settings.sideButtonLayout != originalSide,
                      "settings save failure does not remap active button input");
    press(papyrix::Button::Right);
    press(papyrix::Button::Up);
    press(papyrix::Button::Center);
    runner.expectTrue(ESP.restartCount == 1 && savedBeforeRestart, "changed batch saves before exactly one restart");
    runner.expectTrue(settingsLoadedBeforeRestart && settingsAtRestart.autoSleepMinutes == expectedSleep &&
                          settingsAtRestart.frontButtonLayout == originalFront &&
                          settingsAtRestart.sideButtonLayout == originalSide,
                      "restart reloads all unrelated pending settings from the real settings file");
    runner.expectTrue(papyrix::board::murphyM4Batch() == papyrix::board::MurphyM4Batch::Second,
                      "successful save leaves current touch and display configuration unchanged");
    papyrix::board::loadMurphyM4Batch();
    runner.expectTrue(papyrix::board::murphyM4Batch() == papyrix::board::MurphyM4Batch::First,
                      "next boot loads the saved selection");
#else
    press(papyrix::Button::Right);
    runner.expectTrue(Preferences::putCalls == 0 && ESP.restartCount == 0, "non-M4 Settings has no batch action");
#endif
    papyrix::hal::Display::onFlush = nullptr;
    activeRenderer = nullptr;
    captureNext.clear();
  }
  ESP.beforeRestart = nullptr;
  return runner.allPassed() ? 0 : 1;
}
