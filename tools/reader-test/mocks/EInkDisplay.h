#pragma once

#include <cstdint>
#include <cstring>

class EInkDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };

#ifdef PAPYRIX_TEST_X3
  // X3 panel: 792x528 landscape. Build with -DPAPYRIX_TEST_X3 to lay out pages
  // as if running on the X3 hardware (528x792 portrait viewport).
  static constexpr uint16_t DISPLAY_WIDTH = 792;
  static constexpr uint16_t DISPLAY_HEIGHT = 528;
#else
  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
#endif
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  EInkDisplay(int8_t, int8_t, int8_t, int8_t, int8_t, int8_t) { memset(frameBuffer_, 0xFF, BUFFER_SIZE); }

  uint8_t* getFrameBuffer() const { return const_cast<uint8_t*>(frameBuffer_); }
  void clearScreen(uint8_t color = 0xFF) { memset(frameBuffer_, color, BUFFER_SIZE); }
  void displayBuffer(RefreshMode, bool) {}
  void displayBufferDriveAll(bool = false) {}
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
