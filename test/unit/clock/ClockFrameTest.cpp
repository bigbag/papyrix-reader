#include <GfxRenderer.h>
#include <builtinFonts/small14.h>
#include <builtinFonts/ui_12.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <vector>

#include "apps/ClockApp.h"
#include "apps/ClockPower.h"
#include "config.h"
#include "test_utils.h"

namespace {
EpdFont uiFont(&ui_12), smallFont(&small14);
unsigned refreshes = 0;
void fonts(GfxRenderer& renderer) {
  renderer.begin();
  renderer.insertFont(UI_FONT_ID, EpdFontFamily(&uiFont));
  renderer.insertFont(SMALL_FONT_ID, EpdFontFamily(&smallFont));
}
void zone(const char* value) {
#ifdef _WIN32
  _putenv_s("TZ", value);
  _tzset();
#else
  setenv("TZ", value, 1);
  tzset();
#endif
}
}

int main() {
  using namespace papyrix;
  using namespace clock_app;
  TestUtils::TestRunner runner("Clock frame reconstruction");
  hal::Display::onFlush = [](const hal::Display&) { ++refreshes; };
  for (const bool x3 : {false, true}) {
    hal::Display panel;
    if (x3) panel.setDisplayX3();
    GfxRenderer renderer(panel);
    fonts(renderer);
    for (const auto face : clock_faces::SELECTABLE_FACES) {
      for (const bool dark : {false, true}) {
        for (uint8_t orientation = 0; orientation < 4; ++orientation) {
          for (const bool interactive : {false, true}) {
            ClockFrame frame{};
            frame.utcSeconds = 1789304040;
            frame.face = face;
            frame.utcOffset = 7;
            frame.dateFormat = 3;
            frame.batteryPercent = 61;
            frame.usbConnected = 1;
            frame.interactive = interactive;
            frame.appearance.orientation = orientation;
            frame.appearance.frontButtonLayout = 1;
            frame.appearance.invertedMode = dark;
            frame.appearance.backgroundColor = dark ? 0 : 255;
            frame.appearance.primaryTextBlack = frame.appearance.secondaryTextBlack = !dark;
            zone("UTC0");
            renderFrame(renderer, frame);
            const std::vector<uint8_t> previous(panel.getFrameBuffer(), panel.getFrameBuffer() + panel.getBufferSize());
            auto changed = frame;
            changed.utcSeconds += 86460;
            changed.utcOffset = -12;
            changed.batteryPercent = 20;
            changed.usbConnected = 0;
            renderFrame(renderer, changed);
            runner.expectFalse(std::equal(previous.begin(), previous.end(), panel.getFrameBuffer()),
                               "a new time and battery state change the clock image");
            zone("UTC-13");
            hal::Display restartedPanel;
            if (x3) restartedPanel.setDisplayX3();
            GfxRenderer restarted(restartedPanel);
            fonts(restarted);
            restarted.setOrientation(GfxRenderer::LandscapeClockwise);
            restarted.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
            renderFrame(restarted, frame);
            runner.expectTrue(std::equal(previous.begin(), previous.end(), restartedPanel.getFrameBuffer()),
                              "retained inputs reconstruct every pixel after reset and a timezone change");
          }
        }
      }
    }
    renderFrame(renderer, ClockFrame{});
  }
  runner.expectEq(0U, refreshes, "frame reconstruction never refreshes the panel, including invalid time");
  hal::Display::onFlush = nullptr;
  return runner.allPassed() ? 0 : 1;
}
