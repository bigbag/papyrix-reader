#include "test_utils.h"

#include <EInkDisplay.h>
#include <algorithm>
#include <cstring>

class ExternalFont;
class StreamingEpdFont;

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation {
    Portrait,
    LandscapeClockwise,
    PortraitInverted,
    LandscapeCounterClockwise
  };

  explicit GfxRenderer(EInkDisplay& display) : einkDisplay(display), orientation(Portrait), renderMode(BW) {}

  void begin() { frameBuffer = einkDisplay.getFrameBuffer(); }
  void setOrientation(const Orientation o) { orientation = o; }
  void setRenderMode(const RenderMode m) { renderMode = m; }
  uint8_t* getFrameBuffer() const { return frameBuffer; }
  void clearScreen(uint8_t color = 0xFF) const { einkDisplay.clearScreen(color); }

  int getScreenWidth() const {
    switch (orientation) {
      case Portrait:
      case PortraitInverted:
        return einkDisplay.getDisplayHeight();
      case LandscapeClockwise:
      case LandscapeCounterClockwise:
        return einkDisplay.getDisplayWidth();
    }
    return einkDisplay.getDisplayHeight();
  }

  int getScreenHeight() const {
    switch (orientation) {
      case Portrait:
      case PortraitInverted:
        return einkDisplay.getDisplayWidth();
      case LandscapeClockwise:
      case LandscapeCounterClockwise:
        return einkDisplay.getDisplayHeight();
    }
    return einkDisplay.getDisplayWidth();
  }

  void drawPixel(const int x, const int y, const bool state = true) const {
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

  // Reference implementation: per-pixel bounds check + drawPixel (known correct)
  void renderCharRef(const uint8_t* bitmap, int bmWidth, int bmHeight, bool is2Bit, int left, int top,
                     int* x, const int* y, bool pixelState) const {
    const int screenW = getScreenWidth();
    const int screenH = getScreenHeight();

    for (int glyphY = 0; glyphY < bmHeight; glyphY++) {
      const int screenY = *y - top + glyphY;
      if (screenY < 0 || screenY >= screenH) continue;

      for (int glyphX = 0; glyphX < bmWidth; glyphX++) {
        const int screenX = *x + left + glyphX;
        if (screenX < 0 || screenX >= screenW) continue;

        const int pixelPos = glyphY * bmWidth + glyphX;

        if (is2Bit) {
          const uint8_t byte = bitmap[pixelPos / 4];
          const uint8_t bitIdx = (3 - pixelPos % 4) * 2;
          const uint8_t bmpVal = (3 - (byte >> bitIdx)) & 0x3;

          if (renderMode == BW && bmpVal < 3) {
            drawPixel(screenX, screenY, pixelState);
          } else if (renderMode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
            drawPixel(screenX, screenY, false);
          } else if (renderMode == GRAYSCALE_LSB && bmpVal == 1) {
            drawPixel(screenX, screenY, false);
          }
        } else {
          const uint8_t byte = bitmap[pixelPos / 8];
          const uint8_t bitIdx = 7 - (pixelPos % 8);
          if ((byte >> bitIdx) & 1) {
            drawPixel(screenX, screenY, pixelState);
          }
        }
      }
    }
  }

  // Optimized implementation: pre-clipped + direct FB writes (under test)
  void renderCharOpt(const uint8_t* bitmap, int bmWidth, int bmHeight, bool is2Bit, int left, int top,
                     int* x, const int* y, bool pixelState) const {
    const int screenW = getScreenWidth();
    const int screenH = getScreenHeight();
    const int logLeft = *x + left;
    const int logTop = *y - top;

    const int gxStart = std::max(0, -logLeft);
    const int gxEnd = std::min(bmWidth, screenW - logLeft);
    const int gyStart = std::max(0, -logTop);
    const int gyEnd = std::min(bmHeight, screenH - logTop);

    if (gxStart >= gxEnd || gyStart >= gyEnd) return;

    const int panelW = static_cast<int>(EInkDisplay::DISPLAY_WIDTH);
    const int panelH = static_cast<int>(EInkDisplay::DISPLAY_HEIGHT);
    const int stride = static_cast<int>(EInkDisplay::DISPLAY_WIDTH_BYTES);

    auto extract = [&](int pixelPos, bool& outState) -> bool {
      if (is2Bit) {
        const uint8_t byte = bitmap[pixelPos / 4];
        const uint8_t bitIdx = static_cast<uint8_t>((3 - pixelPos % 4) * 2);
        const uint8_t bmpVal = (3 - (byte >> bitIdx)) & 0x3;
        if (renderMode == BW && bmpVal < 3) { outState = pixelState; return true; }
        if (renderMode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) { outState = false; return true; }
        if (renderMode == GRAYSCALE_LSB && bmpVal == 1) { outState = false; return true; }
        return false;
      }
      const uint8_t byte = bitmap[pixelPos / 8];
      if ((byte >> (7 - (pixelPos % 8))) & 1) { outState = pixelState; return true; }
      return false;
    };

    auto writeFB = [&](int physX, int physY, bool state) {
      const int idx = physY * stride + (physX >> 3);
      const uint8_t bit = static_cast<uint8_t>(1 << (7 - (physX & 7)));
      if (state)
        frameBuffer[idx] &= ~bit;
      else
        frameBuffer[idx] |= bit;
    };

#define INNER_LOOP(PHYS_X_EXPR, PHYS_Y_EXPR)                       \
  for (int gy = gyStart; gy < gyEnd; gy++) {                        \
    const int sY = logTop + gy;                                     \
    for (int gx = gxStart; gx < gxEnd; gx++) {                     \
      bool st;                                                      \
      if (!extract(gy * bmWidth + gx, st)) continue;                \
      const int sX = logLeft + gx;                                  \
      writeFB((PHYS_X_EXPR), (PHYS_Y_EXPR), st);                   \
    }                                                               \
  }

    switch (orientation) {
      case LandscapeCounterClockwise:
        INNER_LOOP(sX, sY)
        break;
      case Portrait:
        INNER_LOOP(sY, panelH - 1 - sX)
        break;
      case LandscapeClockwise:
        INNER_LOOP(panelW - 1 - sX, panelH - 1 - sY)
        break;
      case PortraitInverted:
        INNER_LOOP(panelW - 1 - sY, sX)
        break;
    }

#undef INNER_LOOP
  }

 private:
  EInkDisplay& einkDisplay;
  Orientation orientation;
  RenderMode renderMode;
  uint8_t* frameBuffer = nullptr;
};

static bool buffersMatch(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, EInkDisplay::BUFFER_SIZE) == 0;
}

static bool bufferIsClean(const uint8_t* fb, uint8_t expected) {
  for (uint32_t i = 0; i < EInkDisplay::BUFFER_SIZE; i++) {
    if (fb[i] != expected) return false;
  }
  return true;
}

// 1-bit test glyph: 6x8, checkerboard pattern
// Row 0: 101010 = 0xA8 (top 6 bits)
// Row 1: 010101 = 0x54
// Row 2: 101010 = 0xA8  etc.
static const uint8_t kGlyph1bit[] = {
    0xA8, 0x54, 0xA8, 0x54, 0xA8, 0x54, 0xA8, 0x54  // 6 pixels per row, 1 byte per row (MSB)
};
static constexpr int kGlyph1bitW = 6;
static constexpr int kGlyph1bitH = 8;

// 2-bit test glyph: 4x4
// Pixel values (after inversion via (3-raw)&3):
// Row 0: 0(black) 1(dark) 2(light) 3(white)
// Row 1: 3 2 1 0
// Row 2: 0 1 2 3
// Row 3: 3 2 1 0
// Raw font values (before inversion): 3 2 1 0  |  0 1 2 3  |  3 2 1 0  |  0 1 2 3
// Packed 2-bit MSB-first: byte = (p0<<6)|(p1<<4)|(p2<<2)|p3
// Row 0 raw: 3,2,1,0 → 0b11_10_01_00 = 0xE4
// Row 1 raw: 0,1,2,3 → 0b00_01_10_11 = 0x1B
// Row 2 raw: 3,2,1,0 → 0xE4
// Row 3 raw: 0,1,2,3 → 0x1B
static const uint8_t kGlyph2bit[] = {0xE4, 0x1B, 0xE4, 0x1B};
static constexpr int kGlyph2bitW = 4;
static constexpr int kGlyph2bitH = 4;

// Helper to run ref + opt on both displays and compare
struct DualRender {
  EInkDisplay dispRef{0, 0, 0, 0, 0, 0};
  EInkDisplay dispOpt{0, 0, 0, 0, 0, 0};
  GfxRenderer gfxRef{dispRef};
  GfxRenderer gfxOpt{dispOpt};

  DualRender() {
    gfxRef.begin();
    gfxOpt.begin();
  }

  void setup(GfxRenderer::Orientation orient, GfxRenderer::RenderMode mode = GfxRenderer::BW,
             uint8_t bg = 0xFF) {
    gfxRef.setOrientation(orient);
    gfxOpt.setOrientation(orient);
    gfxRef.setRenderMode(mode);
    gfxOpt.setRenderMode(mode);
    gfxRef.clearScreen(bg);
    gfxOpt.clearScreen(bg);
  }

  bool matches() const { return buffersMatch(gfxRef.getFrameBuffer(), gfxOpt.getFrameBuffer()); }
};

int main() {
  TestUtils::TestRunner runner("GfxRendererRenderChar");

  // Test 1: 1-bit glyph, all 4 orientations
  {
    GfxRenderer::Orientation orients[] = {
        GfxRenderer::LandscapeCounterClockwise,
        GfxRenderer::Portrait,
        GfxRenderer::LandscapeClockwise,
        GfxRenderer::PortraitInverted};
    const char* names[] = {"lccw", "portrait", "lcw", "portrait_inv"};

    for (int i = 0; i < 4; i++) {
      DualRender d;
      d.setup(orients[i]);

      int xRef = 100, xOpt = 100;
      int y = 50;
      d.gfxRef.renderCharRef(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xRef, &y, true);
      d.gfxOpt.renderCharOpt(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xOpt, &y, true);

      runner.expectTrue(d.matches(), std::string("1bit_") + names[i]);
      runner.expectTrue(!bufferIsClean(d.gfxRef.getFrameBuffer(), 0xFF),
                        std::string("1bit_") + names[i] + "_drew_pixels");
    }
  }

  // Test 2: 2-bit glyph, BW mode — all 4 orientations
  {
    GfxRenderer::Orientation orients[] = {
        GfxRenderer::LandscapeCounterClockwise,
        GfxRenderer::Portrait,
        GfxRenderer::LandscapeClockwise,
        GfxRenderer::PortraitInverted};
    const char* names[] = {"lccw", "portrait", "lcw", "portrait_inv"};

    for (int i = 0; i < 4; i++) {
      DualRender d;
      d.setup(orients[i]);

      int xRef = 200, xOpt = 200;
      int y = 100;
      d.gfxRef.renderCharRef(kGlyph2bit, kGlyph2bitW, kGlyph2bitH, true, 1, 3, &xRef, &y, true);
      d.gfxOpt.renderCharOpt(kGlyph2bit, kGlyph2bitW, kGlyph2bitH, true, 1, 3, &xOpt, &y, true);

      runner.expectTrue(d.matches(), std::string("2bit_bw_") + names[i]);
    }
  }

  // Test 3: 2-bit glyph, GRAYSCALE_MSB mode
  {
    DualRender d;
    d.setup(GfxRenderer::LandscapeCounterClockwise, GfxRenderer::GRAYSCALE_MSB);

    int xRef = 50, xOpt = 50;
    int y = 50;
    d.gfxRef.renderCharRef(kGlyph2bit, kGlyph2bitW, kGlyph2bitH, true, 0, 2, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph2bit, kGlyph2bitW, kGlyph2bitH, true, 0, 2, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "2bit_grayscale_msb");
  }

  // Test 4: 2-bit glyph, GRAYSCALE_LSB mode
  {
    DualRender d;
    d.setup(GfxRenderer::LandscapeCounterClockwise, GfxRenderer::GRAYSCALE_LSB);

    int xRef = 50, xOpt = 50;
    int y = 50;
    d.gfxRef.renderCharRef(kGlyph2bit, kGlyph2bitW, kGlyph2bitH, true, 0, 2, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph2bit, kGlyph2bitW, kGlyph2bitH, true, 0, 2, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "2bit_grayscale_lsb");
  }

  // Test 5: Clipped at left edge
  {
    DualRender d;
    d.setup(GfxRenderer::LandscapeCounterClockwise);

    int xRef = -3, xOpt = -3;
    int y = 10;
    d.gfxRef.renderCharRef(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "clip_left");
  }

  // Test 6: Clipped at right edge
  {
    DualRender d;
    d.setup(GfxRenderer::LandscapeCounterClockwise);

    int screenW = d.gfxRef.getScreenWidth();
    int xRef = screenW - 3, xOpt = screenW - 3;
    int y = 10;
    d.gfxRef.renderCharRef(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "clip_right");
  }

  // Test 7: Clipped at top edge
  {
    DualRender d;
    d.setup(GfxRenderer::Portrait);

    int xRef = 100, xOpt = 100;
    int y = 2;  // top = 6, so screenY starts at 2-6 = -4
    d.gfxRef.renderCharRef(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "clip_top");
  }

  // Test 8: Clipped at bottom edge
  {
    DualRender d;
    d.setup(GfxRenderer::Portrait);

    int screenH = d.gfxRef.getScreenHeight();
    int xRef = 100, xOpt = 100;
    int y = screenH - 2;  // top = 6, so screenY starts at screenH-8, ending at screenH-1
    d.gfxRef.renderCharRef(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "clip_bottom");
  }

  // Test 9: Clipped at corner (top-left)
  {
    DualRender d;
    d.setup(GfxRenderer::LandscapeClockwise);

    int xRef = -2, xOpt = -2;
    int y = 3;  // top=6 → screenY starts at -3
    d.gfxRef.renderCharRef(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "clip_corner");
  }

  // Test 10: Entirely off-screen — no pixels drawn
  {
    DualRender d;
    d.setup(GfxRenderer::LandscapeCounterClockwise);

    int xRef = -100, xOpt = -100;
    int y = -100;
    d.gfxRef.renderCharRef(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "offscreen_no_crash");
    runner.expectTrue(bufferIsClean(d.gfxOpt.getFrameBuffer(), 0xFF), "offscreen_no_pixels");
  }

  // Test 11: Zero-size glyph (width=0, height=0)
  {
    DualRender d;
    d.setup(GfxRenderer::LandscapeCounterClockwise);

    int xRef = 100, xOpt = 100;
    int y = 100;
    d.gfxRef.renderCharRef(kGlyph1bit, 0, 0, false, 0, 0, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph1bit, 0, 0, false, 0, 0, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "zero_size");
    runner.expectTrue(bufferIsClean(d.gfxOpt.getFrameBuffer(), 0xFF), "zero_size_no_pixels");
  }

  // Test 12: Negative left offset (italic overhang)
  {
    DualRender d;
    d.setup(GfxRenderer::PortraitInverted);

    int xRef = 5, xOpt = 5;
    int y = 50;
    d.gfxRef.renderCharRef(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, -3, 6, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, -3, 6, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "negative_left");
  }

  // Test 13: pixelState=false (inverted: white pixels on black background)
  {
    DualRender d;
    d.setup(GfxRenderer::LandscapeCounterClockwise, GfxRenderer::BW, 0x00);

    int xRef = 50, xOpt = 50;
    int y = 50;
    d.gfxRef.renderCharRef(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xRef, &y, false);
    d.gfxOpt.renderCharOpt(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xOpt, &y, false);

    runner.expectTrue(d.matches(), "pixel_state_false");
  }

  // Test 14: 2-bit glyph clipped, GRAYSCALE_MSB, Portrait
  {
    DualRender d;
    d.setup(GfxRenderer::Portrait, GfxRenderer::GRAYSCALE_MSB);

    int xRef = -1, xOpt = -1;
    int y = 50;
    d.gfxRef.renderCharRef(kGlyph2bit, kGlyph2bitW, kGlyph2bitH, true, 0, 2, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph2bit, kGlyph2bitW, kGlyph2bitH, true, 0, 2, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "2bit_clipped_grayscale_portrait");
  }

  // Test 15: Far off right edge + bottom edge in LandscapeClockwise
  {
    DualRender d;
    d.setup(GfxRenderer::LandscapeClockwise);

    int screenW = d.gfxRef.getScreenWidth();
    int screenH = d.gfxRef.getScreenHeight();
    int xRef = screenW + 10, xOpt = screenW + 10;
    int y = screenH + 10;
    d.gfxRef.renderCharRef(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xRef, &y, true);
    d.gfxOpt.renderCharOpt(kGlyph1bit, kGlyph1bitW, kGlyph1bitH, false, 0, 6, &xOpt, &y, true);

    runner.expectTrue(d.matches(), "far_offscreen_lcw");
    runner.expectTrue(bufferIsClean(d.gfxOpt.getFrameBuffer(), 0xFF), "far_offscreen_no_pixels");
  }

  return runner.expectTrue(true, "all_done") ? 0 : 1;
}
