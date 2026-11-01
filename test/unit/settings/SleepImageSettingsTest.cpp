#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>
#include <builtinFonts/ui_12.h>
#include <builtinFonts/ui_bold_12.h>

#include <cassert>
#include <filesystem>
#include <fstream>

#include "MurphyM4SettingsFixture.h"
#include "states/SettingsState.h"
#include "test_utils.h"

namespace {
std::string bitmapData() {
  std::string data(58, '\0');
  auto put = [&](size_t offset, uint32_t value) { memcpy(data.data() + offset, &value, sizeof(value)); };
  data[0] = 'B';
  data[1] = 'M';
  put(2, 58);
  put(10, 54);
  put(14, 40);
  put(18, 1);
  put(22, 1);
  data[26] = 1;
  data[28] = 24;
  put(34, 4);
  return data;
}

void capture(const std::filesystem::path& path, const GfxRenderer& renderer, const papyrix::hal::Display& display) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  out << "P1\n" << renderer.getScreenWidth() << ' ' << renderer.getScreenHeight() << '\n';
  for (int y = 0; y < renderer.getScreenHeight(); ++y) {
    for (int x = 0; x < renderer.getScreenWidth(); ++x) {
      papyrix::board::PanelPoint panel{};
      assert(papyrix::board::panelFromLogical(
          static_cast<papyrix::board::DisplayOrientation>(renderer.getOrientation()), 800, 480, x, y, panel));
      out << ((display.getFrameBuffer()[panel.y * 100 + panel.x / 8] & (0x80 >> (panel.x % 8))) ? 0 : 1) << ' ';
    }
    out << '\n';
  }
  assert(out.good());
}
}  // namespace

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("Sleep image selection transactions");
  const std::filesystem::path renderDir = argc == 3 && std::string(argv[1]) == "--render-dir" ? argv[2] : "";
  EpdFont uiRegular(&ui_12), uiBold(&ui_bold_12), readerRegular(&reader_2b), readerBold(&reader_bold_2b);
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (const auto layout : {papyrix::Settings::FrontBCLR, papyrix::Settings::FrontLRBC}) {
      SdMan.reset();
      std::vector<MockDirectoryEntry> entries;
      for (int i = 0; i < 18; ++i) {
        const std::string name = "image-" + std::to_string(i) + ".bmp";
        entries.push_back({name, false});
        SdMan.registerFile("/sleep/" + name, bitmapData());
      }
      SdMan.registerDirectory("/sleep", entries);
      papyrix::Core core;
      assert(core.storage.init().ok());
      core.settings.sleepScreen = papyrix::Settings::SleepCustom;
      core.settings.frontButtonLayout = layout;
      ui::setFrontButtonLayout(layout);
      assert(core.settings.save(core.storage).ok());
      GfxRenderer renderer(core.display);
      renderer.begin();
      renderer.insertFont(UI_FONT_ID, EpdFontFamily(&uiRegular, &uiBold));
      renderer.insertFont(READER_FONT_ID, EpdFontFamily(&readerRegular, &readerBold));
      renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
      papyrix::SettingsState state(renderer);
      state.enter(core);
      auto send = [&](papyrix::Event event) {
        assert(core.events.push(event));
        state.update(core);
      };
      auto press = [&](papyrix::Button button) { send(papyrix::Event::buttonPress(button)); };
      auto draw = [&](const char* name) {
        state.render(core);
        if (!renderDir.empty()) {
          capture(renderDir / std::to_string(orientation) / std::to_string(layout) / name, renderer, core.display);
        }
      };
      auto persistedIs = [&](const char* name) {
        papyrix::Settings decoded;
        return decoded.load(core.storage).ok() && strcmp(decoded.sleepImage, name) == 0;
      };
      press(papyrix::Button::Down);
      press(papyrix::Button::Center);
      press(papyrix::Button::Up);
      draw("screen.pbm");
      press(papyrix::Button::Center);
      draw("random.pbm");
      press(papyrix::Button::Right);
      press(papyrix::Button::Down);
      draw("second-page.pbm");
      press(papyrix::Button::Center);
      runner.expectTrue(strcmp(core.settings.sleepImage, "image-8.bmp") == 0 && persistedIs("image-8.bmp"),
                        "page navigation saves the selected filename immediately");
      press(papyrix::Button::Center);
      press(papyrix::Button::Down);
      press(papyrix::Button::Back);
      runner.expectTrue(persistedIs("image-8.bmp") && strcmp(core.settings.sleepImage, "image-8.bmp") == 0,
                        "Back cancels a different highlighted choice");
      press(papyrix::Button::Center);
      press(papyrix::Button::Down);
      SdMan.setOpenFileForWriteFailCount(1);
      press(papyrix::Button::Center);
      runner.expectTrue(persistedIs("image-8.bmp") && strcmp(core.settings.sleepImage, "image-8.bmp") == 0,
                        "failed save preserves both active and stored choice");
      press(papyrix::Button::Center);
      runner.expectTrue(persistedIs("image-0.bmp"), "retry saves the choice without leaving the picker on error");
      press(papyrix::Button::Center);
      press(papyrix::Button::Up);
      draw("last-page.pbm");
      press(papyrix::Button::Center);
      runner.expectTrue(persistedIs("image-17.bmp"), "Up wraps from Random to the final partial page");
      press(papyrix::Button::Center);
      press(papyrix::Button::Down);
      SdMan.remove("/sleep/image-0.bmp");
      press(papyrix::Button::Center);
      runner.expectTrue(persistedIs("image-17.bmp"), "deleted highlighted image does not replace the saved choice");
      press(papyrix::Button::Back);

      ui::ScreenSettingsView screen;
      screen.values[9] = papyrix::Settings::SleepCustom;
      const int rowHeight = screen.rowHeight(THEME, renderer.getScreenHeight());
      send(papyrix::Event::tap(
          {100, static_cast<int16_t>(60 + (screen.visibleCount() - 1) * rowHeight + rowHeight / 2)}));
      ui::SleepImageView picker;
      const int imageRowHeight = picker.rowHeight(THEME, renderer.getScreenHeight());
      send(papyrix::Event::tap({100, static_cast<int16_t>(60 + imageRowHeight / 2)}));
      runner.expectTrue(persistedIs("") && core.settings.sleepImage[0] == '\0',
                        "touch opens the picker and selects Random in each orientation and button layout");
      press(papyrix::Button::Center);
      press(papyrix::Button::Right);
      draw("saved-random.pbm");
      press(papyrix::Button::Back);
      press(papyrix::Button::Center);
      draw("saved-marker.pbm");
      press(papyrix::Button::Back);
      std::string unicodeName;
      for (int i = 0; i < 126; ++i) unicodeName += u8"Ж";
      unicodeName += ".bmp";
      SdMan.registerFile("/sleep/" + unicodeName, bitmapData());
      SdMan.registerDirectory("/sleep", {{unicodeName, false}});
      press(papyrix::Button::Center);
      press(papyrix::Button::Down);
      draw("unicode-picker.pbm");
      press(papyrix::Button::Center);
      runner.expectTrue(persistedIs(unicodeName.c_str()),
                        "picker saves the complete Unicode filename beyond 255 bytes");
      draw("unicode-selected.pbm");
    }
  }
  ui::setFrontButtonLayout(0);
  return runner.allPassed() ? 0 : 1;
}
