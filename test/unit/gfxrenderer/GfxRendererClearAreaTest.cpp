#include "test_utils.h"

#include <EInkDisplay.h>
#include <algorithm>
#include <cstring>

// Forward declarations to match GfxRenderer.h
class ExternalFont;
class StreamingEpdFont;

// Minimal GfxRenderer with clearArea for testing orientation-aware coordinate rotation
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

  void clearArea(const int x, const int y, const int width, const int height, const uint8_t color) const {
    if (width <= 0 || height <= 0) {
      return;
    }

    // Rotate logical rectangle to physical coordinates
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

    // Validate bounds - region entirely outside display
    if (physX >= static_cast<int>(EInkDisplay::DISPLAY_WIDTH) || physY >= static_cast<int>(EInkDisplay::DISPLAY_HEIGHT) ||
        physX + physW <= 0 || physY + physH <= 0) {
      return;
    }

    // Clamp to display boundaries
    const int x_start = std::max(physX, 0);
    const int y_start = std::max(physY, 0);
    const int x_end = std::min(physX + physW - 1, static_cast<int>(EInkDisplay::DISPLAY_WIDTH - 1));
    const int y_end = std::min(physY + physH - 1, static_cast<int>(EInkDisplay::DISPLAY_HEIGHT - 1));

    // Calculate byte boundaries (8 pixels per byte)
    const int x_byte_start = x_start / 8;
    const int x_byte_end = x_end / 8;
    const int byte_width = x_byte_end - x_byte_start + 1;

    // Clear each row in the region
    for (int row = y_start; row <= y_end; row++) {
      const uint32_t buffer_offset = row * EInkDisplay::DISPLAY_WIDTH_BYTES + x_byte_start;
      memset(&frameBuffer[buffer_offset], color, byte_width);
    }
  }

 private:
  EInkDisplay& einkDisplay;
  Orientation orientation;
  uint8_t* frameBuffer = nullptr;
};

// Helper: count bytes with a specific value in the framebuffer
static int countBytes(const uint8_t* fb, uint8_t value) {
  int count = 0;
  for (uint32_t i = 0; i < EInkDisplay::BUFFER_SIZE; i++) {
    if (fb[i] == value) count++;
  }
  return count;
}

// Helper: check that a physical byte-aligned region has the expected color
// and everything outside is the opposite
static bool isPhysicalRegionCleared(const uint8_t* fb, int physByteStartX, int physStartY,
                                    int byteWidth, int rowCount,
                                    uint8_t regionColor, uint8_t bgColor) {
  for (uint32_t i = 0; i < EInkDisplay::BUFFER_SIZE; i++) {
    int row = i / EInkDisplay::DISPLAY_WIDTH_BYTES;
    int col = i % EInkDisplay::DISPLAY_WIDTH_BYTES;
    bool inRegion = (row >= physStartY && row < physStartY + rowCount &&
                     col >= physByteStartX && col < physByteStartX + byteWidth);
    if (inRegion && fb[i] != regionColor) return false;
    if (!inRegion && fb[i] != bgColor) return false;
  }
  return true;
}

int main() {
  TestUtils::TestRunner runner("GfxRendererClearArea");

  constexpr int W = EInkDisplay::DISPLAY_WIDTH;   // 800
  constexpr int H = EInkDisplay::DISPLAY_HEIGHT;   // 480

  // Test 1: LandscapeCounterClockwise (identity) - basic clearArea
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    // Fill black, clear a byte-aligned region to white
    gfx.clearScreen(0x00);
    gfx.clearArea(0, 0, 16, 2, 0xFF);  // 16px wide = 2 bytes, 2 rows

    // Physical: same as logical (identity). Bytes [0,1] in rows [0,1] should be white
    runner.expectTrue(isPhysicalRegionCleared(gfx.getFrameBuffer(), 0, 0, 2, 2, 0xFF, 0x00),
                      "ccw_basic_16x2");
  }

  // Test 2: LandscapeCounterClockwise - non-origin position
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0x00);
    gfx.clearArea(16, 5, 8, 3, 0xFF);  // 8px = 1 byte at byte col 2, rows 5-7

    runner.expectTrue(isPhysicalRegionCleared(gfx.getFrameBuffer(), 2, 5, 1, 3, 0xFF, 0x00),
                      "ccw_offset_8x3");
  }

  // Test 3: Portrait rotation - swaps width/height and rotates position
  // Portrait: physX=y, physY=H-1-(x+w-1), physW=height, physH=width
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::Portrait);

    gfx.clearScreen(0x00);
    // Logical: x=0, y=0, w=2, h=8 (8px = 1 byte in physical)
    // Physical: physX=0, physY=H-1-(0+2-1)=478, physW=8, physH=2
    gfx.clearArea(0, 0, 2, 8, 0xFF);

    // Physical region: byte col 0, rows 478-479, 1 byte wide, 2 rows
    runner.expectTrue(isPhysicalRegionCleared(gfx.getFrameBuffer(), 0, 478, 1, 2, 0xFF, 0x00),
                      "portrait_2x8_at_origin");
  }

  // Test 4: LandscapeClockwise rotation
  // LandscapeClockwise: physX=W-1-(x+w-1), physY=H-1-(y+h-1), physW=width, physH=height
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeClockwise);

    gfx.clearScreen(0x00);
    // Logical: x=0, y=0, w=8, h=3
    // Physical: physX=W-1-(0+8-1)=792, physY=H-1-(0+3-1)=477, physW=8, physH=3
    gfx.clearArea(0, 0, 8, 3, 0xFF);

    // Physical: byte col 792/8=99, rows 477-479, 1 byte wide, 3 rows
    runner.expectTrue(isPhysicalRegionCleared(gfx.getFrameBuffer(), 99, 477, 1, 3, 0xFF, 0x00),
                      "landscape_cw_8x3_at_origin");
  }

  // Test 5: PortraitInverted rotation
  // PortraitInverted: physX=W-1-(y+h-1), physY=x, physW=height, physH=width
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::PortraitInverted);

    gfx.clearScreen(0x00);
    // Logical: x=0, y=0, w=3, h=8 (8px height becomes 8px physical width = 1 byte)
    // Physical: physX=W-1-(0+8-1)=792, physY=0, physW=8, physH=3
    gfx.clearArea(0, 0, 3, 8, 0xFF);

    // Physical: byte col 792/8=99, rows 0-2, 1 byte wide, 3 rows
    runner.expectTrue(isPhysicalRegionCleared(gfx.getFrameBuffer(), 99, 0, 1, 3, 0xFF, 0x00),
                      "portrait_inv_3x8_at_origin");
  }

  // Test 6: Zero and negative dimensions - no crash, no change
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0x00);
    gfx.clearArea(0, 0, 0, 10, 0xFF);
    gfx.clearArea(0, 0, 10, 0, 0xFF);
    gfx.clearArea(0, 0, -5, 10, 0xFF);
    gfx.clearArea(0, 0, 10, -5, 0xFF);

    runner.expectTrue(countBytes(gfx.getFrameBuffer(), 0xFF) == 0, "zero_negative_dims_no_change");
  }

  // Test 7: Entirely out of bounds - no crash, no change
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0x00);
    gfx.clearArea(W, 0, 8, 8, 0xFF);
    gfx.clearArea(0, H, 8, 8, 0xFF);
    gfx.clearArea(W + 100, H + 100, 8, 8, 0xFF);

    runner.expectTrue(countBytes(gfx.getFrameBuffer(), 0xFF) == 0, "out_of_bounds_no_change");
  }

  // Test 8: Out of bounds in rotated orientation
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::Portrait);

    gfx.clearScreen(0x00);
    // In Portrait, logical viewport is H(480) wide x W(800) tall
    // Logical x=800 should be out of bounds after rotation
    gfx.clearArea(800, 0, 8, 8, 0xFF);

    runner.expectTrue(countBytes(gfx.getFrameBuffer(), 0xFF) == 0, "portrait_oob_no_change");
  }

  // Test 9: Custom color value
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0xFF);
    gfx.clearArea(0, 0, 8, 1, 0xAA);  // 8px = 1 byte

    uint8_t* fb = gfx.getFrameBuffer();
    runner.expectTrue(fb[0] == 0xAA, "custom_color_0xAA");
    runner.expectTrue(fb[1] == 0xFF, "custom_color_adjacent_unchanged");
  }

  // Test 10: Consistency between orientations
  // clearArea at the full display should fill the same number of bytes regardless of orientation
  {
    auto clearAndCount = [](GfxRenderer::Orientation orient) {
      EInkDisplay display(0, 0, 0, 0, 0, 0);
      GfxRenderer gfx(display);
      gfx.begin();
      gfx.setOrientation(orient);
      gfx.clearScreen(0x00);

      // Use byte-aligned dimensions that fit in all orientations
      // LandscapeCW/CCW: 800x480, Portrait/Inv: logical 480x800
      // Clear a small byte-aligned square at origin
      gfx.clearArea(0, 0, 8, 8, 0xFF);
      return countBytes(gfx.getFrameBuffer(), 0xFF);
    };

    int ccw = clearAndCount(GfxRenderer::LandscapeCounterClockwise);
    int cw = clearAndCount(GfxRenderer::LandscapeClockwise);
    int port = clearAndCount(GfxRenderer::Portrait);
    int portInv = clearAndCount(GfxRenderer::PortraitInverted);

    // All orientations should clear 8 rows * 1 byte = 8 bytes
    runner.expectTrue(ccw == 8, "consistency_ccw_8bytes");
    runner.expectTrue(cw == 8, "consistency_cw_8bytes");
    runner.expectTrue(port == 8, "consistency_portrait_8bytes");
    runner.expectTrue(portInv == 8, "consistency_portrait_inv_8bytes");
  }

  // Test 11: Portrait - verify corners of cleared region with drawPixel-style check
  // Clear logical rect (10, 16, 5, 8) in Portrait
  // Portrait: physX=16, physY=H-1-(10+5-1)=466, physW=8, physH=5
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::Portrait);

    gfx.clearScreen(0x00);
    gfx.clearArea(10, 16, 5, 8, 0xFF);

    // Physical region: byte col 16/8=2, rows 465-469, 1 byte wide
    runner.expectTrue(isPhysicalRegionCleared(gfx.getFrameBuffer(), 2, 465, 1, 5, 0xFF, 0x00),
                      "portrait_offset_rect");
  }

  // Test 12: Large region spanning multiple bytes
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    gfx.clearScreen(0x00);
    gfx.clearArea(0, 0, 80, 10, 0xFF);  // 80px = 10 bytes wide, 10 rows

    runner.expectTrue(isPhysicalRegionCleared(gfx.getFrameBuffer(), 0, 0, 10, 10, 0xFF, 0x00),
                      "ccw_large_80x10");
  }

  return runner.allPassed() ? 0 : 1;
}
