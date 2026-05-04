#include "test_utils.h"

#include <EInkDisplay.h>
#include <algorithm>
#include <cstring>

class ExternalFont;
class StreamingEpdFont;

class GfxRenderer {
 public:
  enum Orientation {
    Portrait,
    LandscapeClockwise,
    PortraitInverted,
    LandscapeCounterClockwise
  };

  explicit GfxRenderer(EInkDisplay& display) : einkDisplay(display), orientation(Portrait) {}

  void begin() { frameBuffer = einkDisplay.getFrameBuffer(); }

  void setOrientation(const Orientation o) { orientation = o; }

  uint8_t* getFrameBuffer() const { return frameBuffer; }

  void clearScreen(uint8_t color = 0xFF) const { einkDisplay.clearScreen(color); }

  void drawPixel(const int x, const int y, const bool state) const {
    int rotatedX = 0, rotatedY = 0;
    switch (orientation) {
      case Portrait:
        rotatedX = y;
        rotatedY = EInkDisplay::DISPLAY_HEIGHT - 1 - x;
        break;
      case LandscapeClockwise:
        rotatedX = EInkDisplay::DISPLAY_WIDTH - 1 - x;
        rotatedY = EInkDisplay::DISPLAY_HEIGHT - 1 - y;
        break;
      case PortraitInverted:
        rotatedX = EInkDisplay::DISPLAY_WIDTH - 1 - y;
        rotatedY = x;
        break;
      case LandscapeCounterClockwise:
      default:
        rotatedX = x;
        rotatedY = y;
        break;
    }
    if (rotatedX < 0 || rotatedX >= static_cast<int>(EInkDisplay::DISPLAY_WIDTH) || rotatedY < 0 ||
        rotatedY >= static_cast<int>(EInkDisplay::DISPLAY_HEIGHT)) {
      return;
    }
    const uint16_t byteIndex = rotatedY * EInkDisplay::DISPLAY_WIDTH_BYTES + (rotatedX / 8);
    const uint8_t bitPosition = 7 - (rotatedX % 8);
    if (state)
      frameBuffer[byteIndex] &= ~(1 << bitPosition);
    else
      frameBuffer[byteIndex] |= 1 << bitPosition;
  }

  void fillRect(const int x, const int y, const int width, const int height, const bool state) const {
    if (width <= 0 || height <= 0) return;

    int physX, physY, physW, physH;
    switch (orientation) {
      case Portrait:
        physX = y;
        physY = EInkDisplay::DISPLAY_HEIGHT - 1 - (x + width - 1);
        physW = height;
        physH = width;
        break;
      case LandscapeClockwise:
        physX = EInkDisplay::DISPLAY_WIDTH - 1 - (x + width - 1);
        physY = EInkDisplay::DISPLAY_HEIGHT - 1 - (y + height - 1);
        physW = width;
        physH = height;
        break;
      case PortraitInverted:
        physX = EInkDisplay::DISPLAY_WIDTH - 1 - (y + height - 1);
        physY = x;
        physW = height;
        physH = width;
        break;
      case LandscapeCounterClockwise:
      default:
        physX = x;
        physY = y;
        physW = width;
        physH = height;
        break;
    }

    const int dw = static_cast<int>(EInkDisplay::DISPLAY_WIDTH);
    const int dh = static_cast<int>(EInkDisplay::DISPLAY_HEIGHT);
    if (physX >= dw || physY >= dh || physX + physW <= 0 || physY + physH <= 0) return;

    const int x0 = std::max(physX, 0);
    const int y0 = std::max(physY, 0);
    const int x1 = std::min(physX + physW - 1, dw - 1);
    const int y1 = std::min(physY + physH - 1, dh - 1);

    const int stride = EInkDisplay::DISPLAY_WIDTH_BYTES;
    const int byteStart = x0 / 8;
    const int byteEnd = x1 / 8;

    if (byteStart == byteEnd) {
      const uint8_t mask = static_cast<uint8_t>((0xFF >> (x0 & 7)) & (0xFF << (7 - (x1 & 7))));
      for (int row = y0; row <= y1; row++) {
        uint8_t& b = frameBuffer[row * stride + byteStart];
        if (state)
          b &= ~mask;
        else
          b |= mask;
      }
      return;
    }

    const bool hasLeftEdge = (x0 & 7) != 0;
    const bool hasRightEdge = (x1 & 7) != 7;
    const uint8_t leftMask = static_cast<uint8_t>(0xFF >> (x0 & 7));
    const uint8_t rightMask = static_cast<uint8_t>(0xFF << (7 - (x1 & 7)));
    const uint8_t fill = state ? 0x00 : 0xFF;
    const int fullStart = byteStart + (hasLeftEdge ? 1 : 0);
    const int fullEnd = byteEnd - (hasRightEdge ? 1 : 0);
    const int fullCount = fullEnd - fullStart + 1;

    for (int row = y0; row <= y1; row++) {
      uint8_t* rowPtr = &frameBuffer[row * stride];
      if (hasLeftEdge) {
        if (state)
          rowPtr[byteStart] &= ~leftMask;
        else
          rowPtr[byteStart] |= leftMask;
      }
      if (fullCount > 0) {
        memset(&rowPtr[fullStart], fill, fullCount);
      }
      if (hasRightEdge) {
        if (state)
          rowPtr[byteEnd] &= ~rightMask;
        else
          rowPtr[byteEnd] |= rightMask;
      }
    }
  }

  void fillRectRef(const int x, const int y, const int width, const int height, const bool state) const {
    for (int fillY = y; fillY < y + height; fillY++) {
      for (int fillX = x; fillX < x + width; fillX++) {
        drawPixel(fillX, fillY, state);
      }
    }
  }

 private:
  EInkDisplay& einkDisplay;
  Orientation orientation;
  uint8_t* frameBuffer = nullptr;
};

static bool buffersMatch(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, EInkDisplay::BUFFER_SIZE) == 0;
}

static int countBytes(const uint8_t* fb, uint8_t value) {
  int count = 0;
  for (uint32_t i = 0; i < EInkDisplay::BUFFER_SIZE; i++) {
    if (fb[i] == value) count++;
  }
  return count;
}

int main() {
  TestUtils::TestRunner runner("GfxRendererFillRect");

  constexpr int W = EInkDisplay::DISPLAY_WIDTH;
  constexpr int H = EInkDisplay::DISPLAY_HEIGHT;

  // Test 1: Byte-aligned black fill, identity orientation
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0xFF);
    gfx.fillRect(0, 0, 16, 2, true);

    uint8_t* fb = gfx.getFrameBuffer();
    runner.expectTrue(fb[0] == 0x00 && fb[1] == 0x00, "aligned_black_row0");
    runner.expectTrue(fb[100] == 0x00 && fb[101] == 0x00, "aligned_black_row1");
    runner.expectTrue(fb[2] == 0xFF, "aligned_black_adjacent_unchanged");
    runner.expectTrue(fb[200] == 0xFF, "aligned_black_row2_unchanged");
  }

  // Test 2: Byte-aligned white fill on black background
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0x00);
    gfx.fillRect(8, 1, 24, 3, false);

    uint8_t* fb = gfx.getFrameBuffer();
    for (int row = 1; row <= 3; row++) {
      runner.expectTrue(fb[row * 100 + 1] == 0xFF, "white_fill_byte1_row" + std::to_string(row));
      runner.expectTrue(fb[row * 100 + 2] == 0xFF, "white_fill_byte2_row" + std::to_string(row));
      runner.expectTrue(fb[row * 100 + 3] == 0xFF, "white_fill_byte3_row" + std::to_string(row));
    }
    runner.expectTrue(fb[100 + 0] == 0x00, "white_fill_left_untouched");
    runner.expectTrue(fb[100 + 4] == 0x00, "white_fill_right_untouched");
  }

  // Test 3: Non-aligned edges — compare against reference
  {
    EInkDisplay display1(0, 0, 0, 0, 0, 0);
    EInkDisplay display2(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx1(display1);
    GfxRenderer gfx2(display2);
    gfx1.begin();
    gfx2.begin();
    gfx1.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    gfx2.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx1.clearScreen(0xFF);
    gfx2.clearScreen(0xFF);
    gfx1.fillRect(3, 0, 11, 4, true);
    gfx2.fillRectRef(3, 0, 11, 4, true);

    runner.expectTrue(buffersMatch(gfx1.getFrameBuffer(), gfx2.getFrameBuffer()), "non_aligned_3_11_matches_ref");
  }

  // Test 4: Single-byte rectangle
  {
    EInkDisplay display1(0, 0, 0, 0, 0, 0);
    EInkDisplay display2(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx1(display1);
    GfxRenderer gfx2(display2);
    gfx1.begin();
    gfx2.begin();
    gfx1.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    gfx2.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx1.clearScreen(0xFF);
    gfx2.clearScreen(0xFF);
    gfx1.fillRect(2, 5, 4, 3, true);
    gfx2.fillRectRef(2, 5, 4, 3, true);

    runner.expectTrue(buffersMatch(gfx1.getFrameBuffer(), gfx2.getFrameBuffer()), "single_byte_2_4_matches_ref");
  }

  // Test 5: Full-width status-bar clear (800px = 100 bytes)
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0x00);
    gfx.fillRect(0, 470, 800, 10, false);

    uint8_t* fb = gfx.getFrameBuffer();
    for (int row = 470; row < 480; row++) {
      bool allWhite = true;
      for (int b = 0; b < 100; b++) {
        if (fb[row * 100 + b] != 0xFF) { allWhite = false; break; }
      }
      runner.expectTrue(allWhite, "fullwidth_row" + std::to_string(row));
    }
    runner.expectTrue(fb[469 * 100] == 0x00, "fullwidth_above_untouched");
  }

  // Test 6: Portrait orientation — compare against reference
  {
    EInkDisplay display1(0, 0, 0, 0, 0, 0);
    EInkDisplay display2(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx1(display1);
    GfxRenderer gfx2(display2);
    gfx1.begin();
    gfx2.begin();
    gfx1.setOrientation(GfxRenderer::Portrait);
    gfx2.setOrientation(GfxRenderer::Portrait);

    gfx1.clearScreen(0xFF);
    gfx2.clearScreen(0xFF);
    gfx1.fillRect(10, 5, 20, 12, true);
    gfx2.fillRectRef(10, 5, 20, 12, true);

    runner.expectTrue(buffersMatch(gfx1.getFrameBuffer(), gfx2.getFrameBuffer()), "portrait_matches_ref");
  }

  // Test 7: LandscapeClockwise — compare against reference
  {
    EInkDisplay display1(0, 0, 0, 0, 0, 0);
    EInkDisplay display2(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx1(display1);
    GfxRenderer gfx2(display2);
    gfx1.begin();
    gfx2.begin();
    gfx1.setOrientation(GfxRenderer::LandscapeClockwise);
    gfx2.setOrientation(GfxRenderer::LandscapeClockwise);

    gfx1.clearScreen(0xFF);
    gfx2.clearScreen(0xFF);
    gfx1.fillRect(5, 3, 25, 7, true);
    gfx2.fillRectRef(5, 3, 25, 7, true);

    runner.expectTrue(buffersMatch(gfx1.getFrameBuffer(), gfx2.getFrameBuffer()), "landscape_cw_matches_ref");
  }

  // Test 8: PortraitInverted — compare against reference
  {
    EInkDisplay display1(0, 0, 0, 0, 0, 0);
    EInkDisplay display2(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx1(display1);
    GfxRenderer gfx2(display2);
    gfx1.begin();
    gfx2.begin();
    gfx1.setOrientation(GfxRenderer::PortraitInverted);
    gfx2.setOrientation(GfxRenderer::PortraitInverted);

    gfx1.clearScreen(0xFF);
    gfx2.clearScreen(0xFF);
    gfx1.fillRect(7, 11, 18, 9, true);
    gfx2.fillRectRef(7, 11, 18, 9, true);

    runner.expectTrue(buffersMatch(gfx1.getFrameBuffer(), gfx2.getFrameBuffer()), "portrait_inv_matches_ref");
  }

  // Test 9: Zero/negative dimensions — no crash, no change
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0x00);
    gfx.fillRect(0, 0, 0, 10, true);
    gfx.fillRect(0, 0, 10, 0, true);
    gfx.fillRect(0, 0, -5, 10, true);
    gfx.fillRect(0, 0, 10, -5, true);

    runner.expectTrue(countBytes(gfx.getFrameBuffer(), 0x00) == static_cast<int>(EInkDisplay::BUFFER_SIZE),
                      "zero_negative_no_change");
  }

  // Test 10: Out-of-bounds — no crash, no change
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0x00);
    gfx.fillRect(W, 0, 8, 8, false);
    gfx.fillRect(0, H, 8, 8, false);
    gfx.fillRect(W + 100, H + 100, 8, 8, false);

    runner.expectTrue(countBytes(gfx.getFrameBuffer(), 0x00) == static_cast<int>(EInkDisplay::BUFFER_SIZE),
                      "out_of_bounds_no_change");
  }

  // Test 11: Partial overlap with display edge — clamp correctly
  {
    EInkDisplay display1(0, 0, 0, 0, 0, 0);
    EInkDisplay display2(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx1(display1);
    GfxRenderer gfx2(display2);
    gfx1.begin();
    gfx2.begin();
    gfx1.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    gfx2.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx1.clearScreen(0xFF);
    gfx2.clearScreen(0xFF);
    gfx1.fillRect(790, 475, 20, 10, true);
    gfx2.fillRectRef(790, 475, 20, 10, true);

    runner.expectTrue(buffersMatch(gfx1.getFrameBuffer(), gfx2.getFrameBuffer()), "edge_clamp_matches_ref");
  }

  // Test 12: Edge preservation — fillRect doesn't touch neighboring bits
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0xFF);
    gfx.fillRect(3, 0, 2, 1, true);

    uint8_t* fb = gfx.getFrameBuffer();
    // Pixels 3-4 set to black in byte 0: bits 4,3 cleared
    // Byte 0: 0xFF with bits 4,3 cleared = 0xFF & ~0x18 = 0xE7
    runner.expectTrue(fb[0] == 0xE7, "edge_preserve_byte0");
    runner.expectTrue(fb[1] == 0xFF, "edge_preserve_byte1");
  }

  // Test 13: 1-pixel wide fill
  {
    EInkDisplay display1(0, 0, 0, 0, 0, 0);
    EInkDisplay display2(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx1(display1);
    GfxRenderer gfx2(display2);
    gfx1.begin();
    gfx2.begin();
    gfx1.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    gfx2.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx1.clearScreen(0xFF);
    gfx2.clearScreen(0xFF);
    gfx1.fillRect(5, 10, 1, 20, true);
    gfx2.fillRectRef(5, 10, 1, 20, true);

    runner.expectTrue(buffersMatch(gfx1.getFrameBuffer(), gfx2.getFrameBuffer()), "1px_wide_matches_ref");
  }

  // Test 14: Orientation consistency — same logical rect should fill same pixel count
  {
    auto fillAndCount = [](GfxRenderer::Orientation orient) {
      EInkDisplay display(0, 0, 0, 0, 0, 0);
      GfxRenderer gfx(display);
      gfx.begin();
      gfx.setOrientation(orient);
      gfx.clearScreen(0xFF);
      gfx.fillRect(0, 0, 8, 8, true);

      int blackPixels = 0;
      uint8_t* fb = gfx.getFrameBuffer();
      for (uint32_t i = 0; i < EInkDisplay::BUFFER_SIZE; i++) {
        for (int bit = 0; bit < 8; bit++) {
          if (!(fb[i] & (1 << bit))) blackPixels++;
        }
      }
      return blackPixels;
    };

    int ccw = fillAndCount(GfxRenderer::LandscapeCounterClockwise);
    int cw = fillAndCount(GfxRenderer::LandscapeClockwise);
    int port = fillAndCount(GfxRenderer::Portrait);
    int portInv = fillAndCount(GfxRenderer::PortraitInverted);

    runner.expectTrue(ccw == 64, "consistency_ccw_64px");
    runner.expectTrue(cw == 64, "consistency_cw_64px");
    runner.expectTrue(port == 64, "consistency_portrait_64px");
    runner.expectTrue(portInv == 64, "consistency_portrait_inv_64px");
  }

  return runner.allPassed() ? 0 : 1;
}
