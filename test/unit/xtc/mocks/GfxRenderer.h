#pragma once

#include <EInkDisplay.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

class GfxRenderer {
 public:
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

  explicit GfxRenderer(EInkDisplay& display) : display_(display) {}

  void begin() { frameBuffer_ = display_.getFrameBuffer(); }
  void setOrientation(const Orientation orientation) { orientation_ = orientation; }
  Orientation getOrientation() const { return orientation_; }

  int getScreenWidth() const {
    return orientation_ == Portrait || orientation_ == PortraitInverted ? display_.getDisplayHeight()
                                                                         : display_.getDisplayWidth();
  }
  int getScreenHeight() const {
    return orientation_ == Portrait || orientation_ == PortraitInverted ? display_.getDisplayWidth()
                                                                         : display_.getDisplayHeight();
  }

  void clearScreen(const uint8_t color = 0xFF) const { memset(frameBuffer_, color, display_.getBufferSize()); }

  void drawPixel(const int x, const int y, const bool black = true) const {
    int physicalX;
    int physicalY;
    switch (orientation_) {
      case Portrait:
        physicalX = y;
        physicalY = display_.getDisplayHeight() - 1 - x;
        break;
      case LandscapeClockwise:
        physicalX = display_.getDisplayWidth() - 1 - x;
        physicalY = display_.getDisplayHeight() - 1 - y;
        break;
      case PortraitInverted:
        physicalX = display_.getDisplayWidth() - 1 - y;
        physicalY = x;
        break;
      case LandscapeCounterClockwise:
      default:
        physicalX = x;
        physicalY = y;
        break;
    }
    if (physicalX < 0 || physicalX >= display_.getDisplayWidth() || physicalY < 0 ||
        physicalY >= display_.getDisplayHeight()) {
      return;
    }

    const size_t byteOffset = static_cast<size_t>(physicalY) * display_.getDisplayWidthBytes() + physicalX / 8;
    const uint8_t mask = static_cast<uint8_t>(1U << (7 - physicalX % 8));
    if (black)
      frameBuffer_[byteOffset] &= static_cast<uint8_t>(~mask);
    else
      frameBuffer_[byteOffset] |= mask;
  }

  void copyGrayscaleLsbBuffers() const { display_.copyGrayscaleLsbBuffers(frameBuffer_); }
  void copyGrayscaleMsbBuffers() const { display_.copyGrayscaleMsbBuffers(frameBuffer_); }
  void displayGrayBuffer(const bool turnOffScreen = false) const { display_.displayGrayBuffer(turnOffScreen); }
  void cleanupGrayscaleWithFrameBuffer() const { display_.cleanupGrayscaleBuffers(frameBuffer_); }

  uint8_t* getFrameBuffer() const { return frameBuffer_; }
  size_t getBufferSize() const { return display_.getBufferSize(); }

 private:
  EInkDisplay& display_;
  Orientation orientation_ = Portrait;
  uint8_t* frameBuffer_ = nullptr;
};
