#pragma once

#include <cstdint>
#include <cstring>

// Mock EInkDisplay for GfxRenderer testing
class EInkDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };

  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  EInkDisplay(int8_t, int8_t, int8_t, int8_t, int8_t, int8_t) { memset(frameBuffer_, 0xFF, BUFFER_SIZE); }

  uint8_t* getFrameBuffer() const { return const_cast<uint8_t*>(frameBuffer_); }
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  void clearScreen(uint8_t color = 0xFF) { memset(frameBuffer_, color, BUFFER_SIZE); }
  void displayBuffer(RefreshMode, bool) {}
  void displayWindow(int, int, int, int, bool) {}
  void drawImage(const uint8_t*, int, int, int, int) {}
  void grayscaleRevert() {}
  void copyGrayscaleLsbBuffers(uint8_t*) {}
  void copyGrayscaleMsbBuffers(uint8_t*) {}
  void displayGrayBuffer(bool) {}
  void cleanupGrayscaleBuffers(uint8_t*) {}

 private:
  uint8_t frameBuffer_[BUFFER_SIZE];
};
