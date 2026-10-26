#include <CoverHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <cstdint>
#include <string>
#include <vector>

#include "test_utils.h"
#include "ui/views/ReaderViews.h"

namespace {
template <typename T>
void append(std::string& data, T value) {
  data.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

std::string blackCoverBmp() {
  std::string data = "BM";
  append(data, uint32_t{126});
  append(data, uint32_t{0});
  append(data, uint32_t{62});
  append(data, uint32_t{40});
  append(data, int32_t{16});
  append(data, int32_t{16});
  append(data, uint16_t{1});
  append(data, uint16_t{1});
  append(data, uint32_t{0});
  append(data, uint32_t{64});
  append(data, uint32_t{2835});
  append(data, uint32_t{2835});
  append(data, uint32_t{2});
  append(data, uint32_t{2});
  data.append("\0\0\0\0\377\377\377\0", 8);
  data.append(64, '\0');
  return data;
}

bool blackAt(const uint8_t* buffer, int x, int y) {
  return (buffer[y * papyrix::hal::Display::DISPLAY_WIDTH_BYTES + x / 8] & (0x80 >> (x % 8))) == 0;
}

std::vector<uint8_t> firstCoverFrame;
}  // namespace

int main() {
  TestUtils::TestRunner runner("Reader touch rendering");
  papyrix::hal::Display display;
  GfxRenderer renderer(display);
  renderer.begin();

  struct LayoutCase {
    GfxRenderer::Orientation orientation;
    int left;
    int right;
    int middleY;
    int bottomY;
    const char* description;
  };
  const LayoutCase layouts[] = {
      {GfxRenderer::Portrait, 3, 473, 388, 766, "portrait marks preserve page and footer pixels"},
      {GfxRenderer::LandscapeClockwise, 3, 787, 228, 446, "clockwise marks respect the right panel margin"},
      {GfxRenderer::PortraitInverted, 3, 473, 388, 766, "inverted marks preserve page and footer pixels"},
      {GfxRenderer::LandscapeCounterClockwise, 9, 793, 228, 446,
       "counterclockwise marks respect the left panel margin"},
  };
  for (const auto& layout : layouts) {
    renderer.setOrientation(layout.orientation);
    for (const bool black : {false, true}) {
      renderer.clearScreen(black ? 0xFF : 0x00);
      renderer.fillRect(layout.left + 5, renderer.getScreenHeight() - 18, layout.right - layout.left - 6, 10, black);
      const std::vector<uint8_t> before(renderer.getFrameBuffer(),
                                        renderer.getFrameBuffer() + renderer.getBufferSize());
      ui::readerTouchHints(renderer, black);
      bool pixelsMatch = true;
      for (int py = 0; py < display.getDisplayHeight(); ++py) {
        for (int px = 0; px < display.getDisplayWidth(); ++px) {
          int x, y;
          renderer.panelToLogical(px, py, &x, &y);
          const bool onSide = (x >= layout.left && x < layout.left + 4) || (x >= layout.right && x < layout.right + 4);
          const int inset = x == layout.left + 3 || x == layout.right       ? 2
                            : x == layout.left + 2 || x == layout.right + 1 ? 1
                                                                            : 0;
          const bool inMark = onSide && ((y >= layout.middleY + inset && y < layout.middleY + 24 - inset) ||
                                         (y >= layout.bottomY + inset && y < layout.bottomY + 18 - inset));
          const bool expected = inMark ? black : blackAt(before.data(), px, py);
          pixelsMatch = pixelsMatch && blackAt(renderer.getFrameBuffer(), px, py) == expected;
        }
      }
      runner.expectTrue(pixelsMatch, layout.description);
    }
  }

  renderer.setOrientation(GfxRenderer::Portrait);
  SdMan.reset();
  SdMan.registerFile("/cover.bmp", blackCoverBmp());
  papyrix::hal::Display::onFlush = [](const papyrix::hal::Display& panel) {
    if (firstCoverFrame.empty()) {
      firstCoverFrame.assign(panel.getFrameBuffer(), panel.getFrameBuffer() + panel.getBufferSize());
    }
  };
  for (const bool touch : {false, true}) {
    firstCoverFrame.clear();
    int refreshCountdown = 4;
    const bool rendered = CoverHelpers::renderCoverFromBmp(renderer, "/cover.bmp", 9, 8, 26, 8, refreshCountdown, 4,
                                                           false, touch ? ui::readerTouchHints : nullptr);
    runner.expectTrue(rendered && firstCoverFrame.size() == renderer.getBufferSize(), "cover reaches the display");
    if (firstCoverFrame.size() != renderer.getBufferSize()) continue;
    runner.expectTrue(blackAt(firstCoverFrame.data(), 400, 475) == touch,
                      "cover first refresh contains the left mark only on touch devices");
    runner.expectTrue(blackAt(firstCoverFrame.data(), 775, 4) == touch,
                      "cover first refresh contains the bottom-right mark only on touch devices");
    runner.expectTrue(blackAt(firstCoverFrame.data(), 391, 239), "cover center keeps image ink");
    runner.expectTrue(!blackAt(firstCoverFrame.data(), 100, 379), "cover background stays clear");
  }
  papyrix::hal::Display::onFlush = nullptr;
  SdMan.reset();
  return runner.allPassed() ? 0 : 1;
}
