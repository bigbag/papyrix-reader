#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// Mock EInkDisplay for GfxRenderer testing
class EInkDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };

  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
  static constexpr uint16_t X3_DISPLAY_WIDTH = 792;
  static constexpr uint16_t X3_DISPLAY_HEIGHT = 528;
  static constexpr uint16_t X3_DISPLAY_WIDTH_BYTES = X3_DISPLAY_WIDTH / 8;
  static constexpr uint32_t X3_BUFFER_SIZE = X3_DISPLAY_WIDTH_BYTES * X3_DISPLAY_HEIGHT;
  static constexpr uint32_t MAX_BUFFER_SIZE = X3_BUFFER_SIZE;

  EInkDisplay(int8_t, int8_t, int8_t, int8_t, int8_t, int8_t) { memset(frameBuffer_, 0xFF, MAX_BUFFER_SIZE); }

  void setDisplayX3() {
    displayWidth_ = X3_DISPLAY_WIDTH;
    displayHeight_ = X3_DISPLAY_HEIGHT;
    displayWidthBytes_ = X3_DISPLAY_WIDTH_BYTES;
    bufferSize_ = X3_BUFFER_SIZE;
    memset(frameBuffer_, 0xFF, MAX_BUFFER_SIZE);
  }
  uint8_t* getFrameBuffer() const { return const_cast<uint8_t*>(frameBuffer_); }
  uint16_t getDisplayWidth() const { return displayWidth_; }
  uint16_t getDisplayHeight() const { return displayHeight_; }
  uint16_t getDisplayWidthBytes() const { return displayWidthBytes_; }
  uint32_t getBufferSize() const { return bufferSize_; }
  void clearScreen(uint8_t color = 0xFF) { memset(frameBuffer_, color, bufferSize_); }
  void displayBuffer(RefreshMode, bool) {}
  void displayBufferDriveAll(bool = false) {}
  void displayWindow(int, int, int, int, bool) {}
  void drawImage(const uint8_t*, int, int, int, int) {}
  void grayscaleRevert() {}
  void copyGrayscaleLsbBuffers(const uint8_t* buffer) {
    grayscaleLsb_.assign(buffer, buffer + bufferSize_);
  }
  void copyGrayscaleMsbBuffers(const uint8_t* buffer) {
    grayscaleMsb_.assign(buffer, buffer + bufferSize_);
  }
  void displayGrayBuffer(bool) { displayGrayCount_++; }
  void cleanupGrayscaleBuffers(const uint8_t* buffer) {
    cleanupBuffer_.assign(buffer, buffer + bufferSize_);
    cleanupCount_++;
  }

  const std::vector<uint8_t>& grayscaleLsb() const { return grayscaleLsb_; }
  const std::vector<uint8_t>& grayscaleMsb() const { return grayscaleMsb_; }
  const std::vector<uint8_t>& cleanupBuffer() const { return cleanupBuffer_; }
  int displayGrayCount() const { return displayGrayCount_; }
  int cleanupCount() const { return cleanupCount_; }

 private:
  uint16_t displayWidth_ = DISPLAY_WIDTH;
  uint16_t displayHeight_ = DISPLAY_HEIGHT;
  uint16_t displayWidthBytes_ = DISPLAY_WIDTH_BYTES;
  uint32_t bufferSize_ = BUFFER_SIZE;
  uint8_t frameBuffer_[MAX_BUFFER_SIZE];
  std::vector<uint8_t> grayscaleLsb_;
  std::vector<uint8_t> grayscaleMsb_;
  std::vector<uint8_t> cleanupBuffer_;
  int displayGrayCount_ = 0;
  int cleanupCount_ = 0;
};
