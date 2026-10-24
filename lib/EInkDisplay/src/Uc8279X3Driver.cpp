#include "Uc8279X3Driver.h"

#if !PAPYRIX_TARGET_STICKY && !PAPYRIX_TARGET_MURPHY_M4

// This driver contains synchronous code from FreeInk SDK commit df4a1b7.
// The license is in ../FREEINK_LICENSE.

#include <algorithm>
#include <cstring>

#include "Uc8279X3Luts.h"

namespace papyrix::eink {
namespace {

constexpr uint8_t CMD_POWER_OFF = 0x02;
constexpr uint8_t CMD_POWER_ON = 0x04;
constexpr uint8_t CMD_DEEP_SLEEP = 0x07;
constexpr uint8_t CMD_DTM1 = 0x10;
constexpr uint8_t CMD_DATA_STOP = 0x11;
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;
constexpr uint8_t CMD_DTM2 = 0x13;
constexpr uint8_t CMD_LUT_VCOM = 0x20;
constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;
constexpr uint8_t CMD_PARTIAL_WINDOW = 0x90;
constexpr uint8_t CMD_PARTIAL_IN = 0x91;
constexpr uint8_t CMD_PARTIAL_OUT = 0x92;
constexpr size_t STREAM_CHUNK_SIZE = 128;

const uint8_t FULL_WINDOW[9] = {0x00, 0x00, 0x03, 0x17, 0x00, 0x00, 0x02, 0x0F, 0x01};

}  // namespace

void Uc8279X3Driver::sendScript(Uc8279Bus& bus, const uint8_t* script, size_t size) {
  size_t offset = 0;
  while (offset + 2 <= size) {
    const uint8_t command = script[offset++];
    const size_t dataSize = script[offset++];
    if (dataSize > size - offset) return;
    bus.command(command);
    for (size_t i = 0; i < dataSize; i++) bus.data(script[offset + i]);
    offset += dataSize;
  }
}

void Uc8279X3Driver::loadBank(Uc8279Bus& bus, const uint8_t (*bank)[43]) {
  for (size_t table = 0; table < 5; table++) {
    bus.command(bank[table][0]);
    bus.data(&bank[table][1], 42);
  }
}

void Uc8279X3Driver::sendPlaneFlipped(Uc8279Bus& bus, uint8_t command, const uint8_t* plane) {
  bus.beginData(command);
  for (int row = HEIGHT - 1; row >= 0; row--) {
    bus.write(plane + static_cast<uint32_t>(row) * WIDTH_BYTES, WIDTH_BYTES);
  }
  bus.endData();
}

void Uc8279X3Driver::sendPlaneFlippedInverted(Uc8279Bus& bus, uint8_t command, const uint8_t* plane) {
  uint8_t chunk[STREAM_CHUNK_SIZE];
  bus.beginData(command);
  for (int row = HEIGHT - 1; row >= 0; row--) {
    const uint8_t* source = plane + static_cast<uint32_t>(row) * WIDTH_BYTES;
    size_t offset = 0;
    while (offset < WIDTH_BYTES) {
      const size_t count = std::min(STREAM_CHUNK_SIZE, static_cast<size_t>(WIDTH_BYTES) - offset);
      for (size_t i = 0; i < count; i++) chunk[i] = static_cast<uint8_t>(~source[offset + i]);
      bus.write(chunk, count);
      offset += count;
    }
  }
  bus.endData();
}

void Uc8279X3Driver::fillPlane(Uc8279Bus& bus, uint8_t command, uint8_t value) {
  uint8_t chunk[STREAM_CHUNK_SIZE];
  memset(chunk, value, sizeof(chunk));
  bus.beginData(command);
  for (uint16_t row = 0; row < HEIGHT; row++) {
    size_t remaining = WIDTH_BYTES;
    while (remaining > 0) {
      const size_t count = std::min(STREAM_CHUNK_SIZE, remaining);
      bus.write(chunk, count);
      remaining -= count;
    }
  }
  bus.endData();
}

void Uc8279X3Driver::fullWindowIn(Uc8279Bus& bus) {
  bus.command(CMD_PARTIAL_IN);
  bus.commandData(CMD_PARTIAL_WINDOW, FULL_WINDOW, sizeof(FULL_WINDOW));
}

void Uc8279X3Driver::loadGrayscaleBank(Uc8279Bus& bus) {
  for (size_t table = 0; table < 5; table++) {
    bus.command(static_cast<uint8_t>(CMD_LUT_VCOM + table));
    bus.data(kUc8279X3_XtfAa[table], sizeof(kUc8279X3_XtfAa[table]));
  }
}

bool Uc8279X3Driver::triggerGrayscaleRefresh(Uc8279Bus& bus, bool turnOff) {
  if (!screenOn_) {
    bus.command(CMD_POWER_ON);
    if (!bus.waitBusy("8279_gray_PON")) {
      invalidateAfterTimeout();
      return false;
    }
    screenOn_ = true;
  }

  bus.command(CMD_DISPLAY_REFRESH);
  if (!bus.waitBusy("8279_gray_DRF")) {
    invalidateAfterTimeout();
    return false;
  }

  if (turnOff) {
    bus.command(CMD_POWER_OFF);
    if (!bus.waitBusy("8279_gray_POF")) {
      recoverAfterPowerOffTimeout();
    } else {
      screenOn_ = false;
    }
  }
  return true;
}

void Uc8279X3Driver::invalidateAfterTimeout() {
  oldPlaneValid_ = false;
  forceFullSync_ = true;
}

void Uc8279X3Driver::recoverAfterPowerOffTimeout() {
  screenOn_ = false;
  forceFullSync_ = true;
}

void Uc8279X3Driver::begin(Uc8279Bus& bus) {
  bus.reset(50);
  sendScript(bus, kUc8279X3_Init, sizeof(kUc8279X3_Init));
  screenOn_ = false;
  firstRefresh_ = true;
  oldPlaneValid_ = false;
  forceFullSync_ = false;
  initialFullsRemaining_ = 2;
  grayscaleMode_ = false;
  lsbValid_ = false;
  msbValid_ = false;
}

void Uc8279X3Driver::display(Uc8279Bus& bus, const uint8_t* frame, Uc8279RefreshMode mode, bool turnOff) {
  if (frame == nullptr) return;

  const bool useGc = mode != Uc8279RefreshMode::Fast || !oldPlaneValid_ || forceFullSync_ || initialFullsRemaining_ > 0;

  bus.command(CMD_PARTIAL_IN);
  if (!oldPlaneValid_) {
    fillPlane(bus, CMD_DTM1, 0xFF);
    bus.command(CMD_DATA_STOP);
  } else if (darkBackground_ && !useGc) {
    sendPlaneFlippedInverted(bus, CMD_DTM1, frame);
    bus.command(CMD_DATA_STOP);
  }
  sendPlaneFlipped(bus, CMD_DTM2, frame);
  bus.command(CMD_DATA_STOP);

  bus.command(CMD_VCOM_DATA_INTERVAL);
  bus.data(firstRefresh_ ? kUc8279X3_CdiFirst : kUc8279X3_CdiLater);
  loadBank(bus, useGc ? kUc8279X3_BwGc : kUc8279X3_BwDu);

  if (!screenOn_) {
    bus.command(CMD_POWER_ON);
    if (!bus.waitBusy("8279_PON")) {
      invalidateAfterTimeout();
      return;
    }
    screenOn_ = true;
  }

  bus.command(CMD_DISPLAY_REFRESH);
  if (!bus.waitBusy("8279_DRF")) {
    invalidateAfterTimeout();
    return;
  }

  bus.command(CMD_VCOM_DATA_INTERVAL);
  bus.data(kUc8279X3_CdiLater);
  sendPlaneFlipped(bus, CMD_DTM1, frame);
  bus.command(CMD_DATA_STOP);
  bus.command(CMD_PARTIAL_OUT);

  oldPlaneValid_ = true;
  firstRefresh_ = false;
  forceFullSync_ = false;
  if (useGc && initialFullsRemaining_ > 0) initialFullsRemaining_--;

  if (turnOff) {
    bus.command(CMD_POWER_OFF);
    if (!bus.waitBusy("8279_POF")) {
      recoverAfterPowerOffTimeout();
      return;
    }
    screenOn_ = false;
  }
}

void Uc8279X3Driver::requestResync() { forceFullSync_ = true; }

void Uc8279X3Driver::copyGrayscaleLsb(Uc8279Bus& bus, const uint8_t* plane) {
  if (plane == nullptr) {
    lsbValid_ = false;
    msbValid_ = false;
    return;
  }

  fullWindowIn(bus);
  sendPlaneFlipped(bus, CMD_DTM1, plane);
  bus.command(CMD_DATA_STOP);
  bus.command(CMD_PARTIAL_OUT);
  oldPlaneValid_ = false;
  lsbValid_ = true;
  msbValid_ = false;
}

void Uc8279X3Driver::copyGrayscaleMsb(Uc8279Bus& bus, const uint8_t* plane) {
  if (plane == nullptr || !lsbValid_) return;

  fullWindowIn(bus);
  sendPlaneFlipped(bus, CMD_DTM2, plane);
  bus.command(CMD_DATA_STOP);
  bus.command(CMD_PARTIAL_OUT);
  msbValid_ = true;
}

void Uc8279X3Driver::displayGray(Uc8279Bus& bus, bool turnOff) {
  if (!lsbValid_ || !msbValid_) return;

  fullWindowIn(bus);
  loadGrayscaleBank(bus);
  bus.command(CMD_VCOM_DATA_INTERVAL);
  bus.data(firstRefresh_ ? kUc8279X3_CdiFirst : kUc8279X3_CdiLater);
  const bool refreshed = triggerGrayscaleRefresh(bus, turnOff);
  bus.command(CMD_PARTIAL_OUT);
  lsbValid_ = false;
  msbValid_ = false;
  if (!refreshed) return;

  grayscaleMode_ = true;
  firstRefresh_ = false;
  oldPlaneValid_ = false;
  forceFullSync_ = false;
}

void Uc8279X3Driver::cleanupGrayscale(Uc8279Bus& bus, const uint8_t* bwFrame) {
  if (bwFrame == nullptr) return;

  fullWindowIn(bus);
  sendPlaneFlipped(bus, CMD_DTM2, bwFrame);
  bus.command(CMD_DATA_STOP);
  sendPlaneFlipped(bus, CMD_DTM1, bwFrame);
  bus.command(CMD_DATA_STOP);
  bus.command(CMD_PARTIAL_OUT);

  grayscaleMode_ = false;
  lsbValid_ = false;
  msbValid_ = false;
  oldPlaneValid_ = true;
  forceFullSync_ = false;
}

void Uc8279X3Driver::grayscaleRevert(Uc8279Bus& bus) {
  if (!grayscaleMode_) return;
  grayscaleMode_ = false;
  lsbValid_ = false;
  msbValid_ = false;

  fullWindowIn(bus);
  fillPlane(bus, CMD_DTM1, 0xFF);
  bus.command(CMD_DATA_STOP);
  fillPlane(bus, CMD_DTM2, 0xFF);
  bus.command(CMD_DATA_STOP);
  bus.command(CMD_VCOM_DATA_INTERVAL);
  bus.data(kUc8279X3_CdiLater);
  loadBank(bus, kUc8279X3_BwGc);
  const bool refreshed = triggerGrayscaleRefresh(bus, false);
  bus.command(CMD_PARTIAL_OUT);
  if (!refreshed) return;

  oldPlaneValid_ = true;
  forceFullSync_ = false;
}

void Uc8279X3Driver::deepSleep(Uc8279Bus& bus) {
  if (screenOn_) {
    bus.command(CMD_POWER_OFF);
    bus.waitBusy("8279_power_down");
  }
  bus.command(CMD_DEEP_SLEEP);
  bus.data(0xA5);

  screenOn_ = false;
  oldPlaneValid_ = false;
  forceFullSync_ = false;
  grayscaleMode_ = false;
  lsbValid_ = false;
  msbValid_ = false;
}

Uc8279X3Driver& uc8279X3Driver() {
  static Uc8279X3Driver driver;
  return driver;
}

}  // namespace papyrix::eink

#endif
