#pragma once

// Minimal GfxRenderer stub for tests linking real text-layout and cover code.
// Members mirror the real GfxRenderer.h signatures so all callers compile;
// display-path members are no-ops and text metrics are constants because the
// tests that use this stub never render to a display or measure real glyphs.

#include <EInkDisplay.h>
#include <EpdFontFamily.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Bitmap;

class GfxRenderer {
 public:
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

  explicit GfxRenderer(EInkDisplay& display) : display_(display) {}

  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }

  // Text metrics (constants)
  int getTextWidth(int, const char*, EpdFontFamily::Style = EpdFontFamily::REGULAR) const { return 10; }
  void drawText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void drawCenteredText(int, int, const char*, bool = true,
                        EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  int getSpaceWidth(int) const { return 5; }
  int getFontAscenderSize(int) const { return 10; }
  int getLineHeight(int) const { return 20; }
  int getEffectiveLineHeight(int) const { return 24; }
  void clearWidthCache() const {}
  void warmCodepointsBatch(int, const uint32_t*, size_t, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  bool fontSupportsGrayscale(int) const { return false; }

  // Display path (no-ops)
  void clearScreen(const uint8_t = 0xFF) const {}
  void clearArea(int, int, int, int, const uint8_t = 0xFF) const {}
  void drawBitmap(Bitmap&, const int, const int, const int, const int) {}
  void displayBuffer(const EInkDisplay::RefreshMode = EInkDisplay::FULL_REFRESH, const bool = false) {}
  bool storeBwBuffer() { return false; }
  void setRenderMode(const RenderMode) {}
  void copyGrayscaleLsbBuffers() {}
  void copyGrayscaleMsbBuffers() {}
  void displayGrayBuffer(const bool = false) {}
  void restoreBwBuffer() {}

  uint8_t* getFrameBuffer() { return nullptr; }
  size_t getBufferSize() const { return 48000; }

 private:
  EInkDisplay& display_;
};
