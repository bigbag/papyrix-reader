#include "Display.h"

#include <EInkDisplay.h>
#include <config.h>

#include "Device.h"

// Global display instance reference (defined in main.cpp)
extern EInkDisplay& display;

namespace papyrix {
namespace drivers {

Result<void> Display::init() {
  if (initialized_) {
    return Ok();
  }

  if (Device::instance().isX3()) {
    display.setDisplayX3();
  }
  display.begin();
  initialized_ = true;
  dirty_ = false;

  return Ok();
}

uint16_t Display::width() const { return display.getDisplayWidth(); }

uint16_t Display::height() const { return display.getDisplayHeight(); }

size_t Display::bufferSize() const { return display.getBufferSize(); }

void Display::shutdown() {
  if (initialized_) {
    display.deepSleep();
    initialized_ = false;
  }
}

uint8_t* Display::getBuffer() { return display.getFrameBuffer(); }

const uint8_t* Display::getBuffer() const { return display.getFrameBuffer(); }

void Display::flush(RefreshMode mode) {
  if (!dirty_ || !initialized_) {
    return;
  }

  EInkDisplay::RefreshMode einkMode;
  switch (mode) {
    case RefreshMode::Full:
      einkMode = EInkDisplay::FULL_REFRESH;
      break;
    case RefreshMode::Half:
      einkMode = EInkDisplay::HALF_REFRESH;
      break;
    case RefreshMode::Fast:
    default:
      einkMode = EInkDisplay::FAST_REFRESH;
      break;
  }

  display.refreshDisplay(einkMode);
  dirty_ = false;
}

void Display::clear(uint8_t color) {
  if (initialized_) {
    display.clearScreen(color);
    dirty_ = true;
  }
}

void Display::sleep() {
  if (initialized_) {
    display.deepSleep();
  }
}

void Display::wake() {
  if (initialized_) {
    display.begin();
  }
}

EInkDisplay& Display::raw() { return display; }

}  // namespace drivers
}  // namespace papyrix
