#include "test_utils.h"

#include <EInkDisplay.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// --- Mock Bitmap ---
// Each row returns a distinct 2bpp value (0..3) so we can verify screen placement.
// Records readRow calls for verification.

enum class BmpReaderError : uint8_t { Ok = 0 };

class Bitmap {
 public:
  Bitmap(int w, int h, bool topDown) : w_(w), h_(h), topDown_(topDown) {}

  int getWidth() const { return w_; }
  int getHeight() const { return h_; }
  bool isTopDown() const { return topDown_; }
  int getRowBytes() const { return w_ * 3; }
  bool hasGreyscale() const { return false; }

  // Fill output row with a 2bpp value derived from rowY (row index 0..h-1).
  // Pattern: each pixel in the row = (rowY % 4), packed 4 pixels per byte MSB-first.
  BmpReaderError readRow(uint8_t* data, uint8_t* /*rowBuffer*/, int rowY) const {
    readRowCalls.push_back(rowY);
    const uint8_t val = static_cast<uint8_t>(rowY % 4);
    const uint8_t packed = static_cast<uint8_t>((val << 6) | (val << 4) | (val << 2) | val);
    const int outBytes = (w_ + 3) / 4;
    for (int i = 0; i < outBytes; i++) data[i] = packed;
    return BmpReaderError::Ok;
  }

  BmpReaderError rewindToData() const { return BmpReaderError::Ok; }

  mutable std::vector<int> readRowCalls;

 private:
  int w_, h_;
  bool topDown_;
};

// --- Minimal GfxRenderer with drawBitmap ---
// Mirrors the real implementation's drawBitmap logic + minimal dependencies.

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

  explicit GfxRenderer(EInkDisplay& display)
      : einkDisplay(display), renderMode(BW), orientation(LandscapeCounterClockwise) {}

  void begin() { frameBuffer = einkDisplay.getFrameBuffer(); }
  void setOrientation(Orientation o) { orientation = o; }
  void setRenderMode(RenderMode m) { renderMode = m; }
  uint8_t* getFrameBuffer() const { return frameBuffer; }

  int getScreenWidth() const {
    switch (orientation) {
      case Portrait:
      case PortraitInverted:
        return EInkDisplay::DISPLAY_HEIGHT;
      case LandscapeClockwise:
      case LandscapeCounterClockwise:
        return EInkDisplay::DISPLAY_WIDTH;
    }
    return EInkDisplay::DISPLAY_HEIGHT;
  }

  int getScreenHeight() const {
    switch (orientation) {
      case Portrait:
      case PortraitInverted:
        return EInkDisplay::DISPLAY_WIDTH;
      case LandscapeClockwise:
      case LandscapeCounterClockwise:
        return EInkDisplay::DISPLAY_HEIGHT;
    }
    return EInkDisplay::DISPLAY_WIDTH;
  }

  void drawPixel(int x, int y, bool state = true) const {
    // LandscapeCounterClockwise = identity mapping for simplicity
    if (x < 0 || x >= static_cast<int>(EInkDisplay::DISPLAY_WIDTH) || y < 0 ||
        y >= static_cast<int>(EInkDisplay::DISPLAY_HEIGHT))
      return;
    const uint16_t byteIndex = y * EInkDisplay::DISPLAY_WIDTH_BYTES + (x / 8);
    const uint8_t bitPosition = 7 - (x % 8);
    if (state)
      frameBuffer[byteIndex] &= ~(1 << bitPosition);
    else
      frameBuffer[byteIndex] |= 1 << bitPosition;
  }

  // Exact copy of the production drawBitmap logic
  void drawBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight) const {
    float scale = 1.0f;
    bool isScaled = false;
    if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
      scale = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
      isScaled = true;
    }
    if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
      scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight()));
      isScaled = true;
    }

    const size_t outputRowSize = static_cast<size_t>((bitmap.getWidth() + 3) / 4);
    std::vector<uint8_t> bitmapOutputRow(outputRowSize);
    std::vector<uint8_t> bitmapRowBytes(bitmap.getRowBytes());

    const int destWidth = isScaled ? static_cast<int>(bitmap.getWidth() * scale) : bitmap.getWidth();
    const int destHeight = isScaled ? static_cast<int>(bitmap.getHeight() * scale) : bitmap.getHeight();
    const float invScale = isScaled ? (1.0f / scale) : 1.0f;

    int lastSrcY = -1;
    for (int destY = 0; destY < destHeight; destY++) {
      const int screenY = bitmap.isTopDown() ? (y + destY) : (y + destHeight - 1 - destY);
      if (screenY < 0) continue;
      if (screenY >= getScreenHeight()) continue;

      int srcY = isScaled ? static_cast<int>(destY * invScale) : destY;
      if (srcY >= bitmap.getHeight()) srcY = bitmap.getHeight() - 1;

      if (srcY != lastSrcY) {
        if (bitmap.readRow(bitmapOutputRow.data(), bitmapRowBytes.data(), srcY) != BmpReaderError::Ok) {
          return;
        }
        lastSrcY = srcY;
      }

      for (int destX = 0; destX < destWidth; destX++) {
        const int screenX = x + destX;
        if (screenX < 0) continue;
        if (screenX >= getScreenWidth()) break;

        int bmpX = isScaled ? static_cast<int>(destX * invScale) : destX;
        if (bmpX >= bitmap.getWidth()) bmpX = bitmap.getWidth() - 1;

        const uint8_t val = bitmapOutputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

        if (renderMode == BW && val < 3) {
          drawPixel(screenX, screenY);
        }
      }
    }
  }

 private:
  EInkDisplay& einkDisplay;
  RenderMode renderMode;
  Orientation orientation;
  uint8_t* frameBuffer = nullptr;
};

// Helper: check if pixel at (x,y) is black (bit cleared) in framebuffer
static bool isPixelSet(const uint8_t* fb, int x, int y) {
  const uint16_t byteIndex = y * EInkDisplay::DISPLAY_WIDTH_BYTES + (x / 8);
  const uint8_t bitPosition = 7 - (x % 8);
  return (fb[byteIndex] & (1 << bitPosition)) == 0;
}

int main() {
  TestUtils::TestRunner runner("GfxRendererDrawBitmap");

  // Test 1: Top-down bitmap - row 0 appears at top of placement
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    // 4x4 bitmap, top-down. Row 0 has val=0 (black), row 3 has val=3 (white in BW mode).
    Bitmap bmp(4, 4, true);
    gfx.drawBitmap(bmp, 10, 20, 0, 0);

    // Row 0 (val=0, black) should be at screenY=20 (top)
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 10, 20), "topdown_row0_at_top");
    // Row 3 (val=3, white in BW mode - val<3 is black) should NOT be drawn at screenY=23
    runner.expectFalse(isPixelSet(gfx.getFrameBuffer(), 10, 23), "topdown_row3_white");
  }

  // Test 2: Bottom-up bitmap - row 0 appears at bottom of placement
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    // 4x4 bitmap, bottom-up. Row 0 (val=0, black) should appear at bottom.
    Bitmap bmp(4, 4, false);
    gfx.drawBitmap(bmp, 10, 20, 0, 0);

    // Row 0 (val=0, black) should be at screenY=23 (bottom of placement)
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 10, 23), "bottomup_row0_at_bottom");
    // Row 3 (val=3, white) should be at screenY=20 (top of placement)
    runner.expectFalse(isPixelSet(gfx.getFrameBuffer(), 10, 20), "bottomup_row3_white_at_top");
  }

  // Test 3: readRow receives sequential srcY values (0,1,2,3) regardless of orientation
  {
    Bitmap bmpTD(4, 4, true);
    Bitmap bmpBU(4, 4, false);

    EInkDisplay d1(0, 0, 0, 0, 0, 0);
    GfxRenderer g1(d1);
    g1.begin();
    g1.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    g1.drawBitmap(bmpTD, 0, 0, 0, 0);

    EInkDisplay d2(0, 0, 0, 0, 0, 0);
    GfxRenderer g2(d2);
    g2.begin();
    g2.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    g2.drawBitmap(bmpBU, 0, 0, 0, 0);

    std::vector<int> expected = {0, 1, 2, 3};
    runner.expectTrue(bmpTD.readRowCalls == expected, "topdown_sequential_readRow");
    runner.expectTrue(bmpBU.readRowCalls == expected, "bottomup_sequential_readRow");
  }

  // Test 4: Bottom-up bitmap - verify each row's pixel value lands at correct screen Y
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    // 4x4 bottom-up bitmap at position (0, 0)
    // Row 0: val=0 (black), Row 1: val=1 (black), Row 2: val=2 (black), Row 3: val=3 (white)
    Bitmap bmp(4, 4, false);
    gfx.drawBitmap(bmp, 0, 0, 0, 0);

    // Bottom-up: destY=0 → screenY=3, destY=1 → screenY=2, destY=2 → screenY=1, destY=3 → screenY=0
    // destY=0 reads srcY=0 (val=0, black) → screenY=3
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 0, 3), "bottomup_srcY0_at_screenY3");
    // destY=1 reads srcY=1 (val=1, black) → screenY=2
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 0, 2), "bottomup_srcY1_at_screenY2");
    // destY=2 reads srcY=2 (val=2, black) → screenY=1
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 0, 1), "bottomup_srcY2_at_screenY1");
    // destY=3 reads srcY=3 (val=3, white in BW) → screenY=0
    runner.expectFalse(isPixelSet(gfx.getFrameBuffer(), 0, 0), "bottomup_srcY3_white_at_screenY0");
  }

  // Test 5: Top-down bitmap - verify each row's pixel value lands at correct screen Y
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    Bitmap bmp(4, 4, true);
    gfx.drawBitmap(bmp, 0, 0, 0, 0);

    // Top-down: destY=0 → screenY=0, destY=1 → screenY=1, etc.
    // destY=0 reads srcY=0 (val=0, black) → screenY=0
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 0, 0), "topdown_srcY0_at_screenY0");
    // destY=1 reads srcY=1 (val=1, black) → screenY=1
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 0, 1), "topdown_srcY1_at_screenY1");
    // destY=3 reads srcY=3 (val=3, white) → screenY=3
    runner.expectFalse(isPixelSet(gfx.getFrameBuffer(), 0, 3), "topdown_srcY3_white_at_screenY3");
  }

  // Test 6: Scaled bitmap - bottom-up with 2x downscale
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    // 8x8 bitmap scaled to fit in 4x4 (maxWidth=4, maxHeight=4)
    // Row 0: val=0 (black), row 7: val=3 (white)
    Bitmap bmp(8, 8, false);
    gfx.drawBitmap(bmp, 0, 0, 4, 4);

    // destHeight = int(8 * 0.5) = 4
    // Bottom-up: destY=0 → screenY=3, destY=3 → screenY=0
    // destY=0: srcY = int(0 * 2.0) = 0 (val=0, black) → screenY=3
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 0, 3), "scaled_bottomup_row0_at_bottom");
    // destY=3: srcY = int(3 * 2.0) = 6 (val=6%4=2, black) → screenY=0
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 0, 0), "scaled_bottomup_row6_at_top");
  }

  // Test 7: Bitmap partially off-screen (negative y) - bottom-up should use continue not break
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    // 4x4 bottom-up at y=-2: screenY range is [-2+3, -2+0] = [1, -2]
    // destY=0 → screenY=-2+3=1, destY=1 → screenY=-2+2=0, destY=2 → screenY=-2+1=-1, destY=3 → screenY=-2+0=-2
    Bitmap bmp(4, 4, false);
    gfx.drawBitmap(bmp, 0, -2, 0, 0);

    // destY=0 (srcY=0, val=0 black) → screenY=1 (visible)
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 0, 1), "partial_offscreen_visible_row");
    // destY=1 (srcY=1, val=1 black) → screenY=0 (visible)
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 0, 0), "partial_offscreen_edge_row");
  }

  // Test 8: Bitmap at bottom edge - bottom-up should use continue for screenY >= screenHeight
  {
    EInkDisplay display(0, 0, 0, 0, 0, 0);
    GfxRenderer gfx(display);
    gfx.begin();
    gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);

    const int screenH = gfx.getScreenHeight();  // 480
    // Place 4px tall bottom-up bitmap at y=screenH-2
    // destY=0 → screenY=(screenH-2)+3=screenH+1 (off-screen), destY=1 → screenY=screenH (off-screen)
    // destY=2 → screenY=screenH-1 (visible), destY=3 → screenY=screenH-2 (visible)
    Bitmap bmp(4, 4, false);
    gfx.drawBitmap(bmp, 0, screenH - 2, 0, 0);

    // destY=2 (srcY=2, val=2 black) → screenY=screenH-1
    runner.expectTrue(isPixelSet(gfx.getFrameBuffer(), 0, screenH - 1), "bottom_edge_visible");
    // destY=3 (srcY=3, val=3 white) → screenY=screenH-2
    runner.expectFalse(isPixelSet(gfx.getFrameBuffer(), 0, screenH - 2), "bottom_edge_white_row");
  }

  return runner.allPassed() ? 0 : 1;
}
