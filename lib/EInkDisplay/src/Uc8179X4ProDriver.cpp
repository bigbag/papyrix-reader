#include "Uc8179X4ProDriver.h"

#if defined(TEST_BUILD) || PAPYRIX_TARGET_X4PRO || PAPYRIX_TARGET_X4CLASSIC

// This driver selectively ports the UC8179 X4 Pro path from FreeInk SDK
// revision 6fabbec80c4d0d7cb6654046caef367a3c750c36. The license is in
// ../FREEINK_LICENSE.

#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifndef TEST_BUILD
#include <esp_heap_caps.h>
#endif

namespace papyrix::eink {
namespace {

constexpr uint8_t CMD_PANEL_SETTING = 0x00;
constexpr uint8_t CMD_POWER_OFF = 0x02;
constexpr uint8_t CMD_POWER_OFF_SEQUENCE = 0x03;
constexpr uint8_t CMD_POWER_ON = 0x04;
constexpr uint8_t CMD_BOOSTER_SOFT_START = 0x06;
constexpr uint8_t CMD_DEEP_SLEEP = 0x07;
constexpr uint8_t CMD_DTM1 = 0x10;
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;
constexpr uint8_t CMD_DTM2 = 0x13;
constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;
constexpr uint8_t CMD_RESOLUTION = 0x61;
constexpr uint8_t CMD_GATE_SOURCE_START = 0x65;
constexpr uint8_t CMD_PARTIAL_WINDOW = 0x90;
constexpr uint8_t CMD_PARTIAL_IN = 0x91;
constexpr uint8_t CMD_PARTIAL_OUT = 0x92;
constexpr uint8_t CMD_CCSET = 0xE0;
constexpr uint8_t CMD_GATE_SCAN = 0xE1;
constexpr uint8_t CMD_POWER_SAVE = 0xE3;
constexpr uint8_t CMD_TSSET = 0xE5;
constexpr uint8_t CDI_INTERVAL = 0x07;
constexpr size_t LUT_SIZE = 42;
constexpr size_t STREAM_CHUNK_SIZE = 128;

const uint8_t GRAY_LUTS[5][LUT_SIZE + 1] = {
    {0x20, 0x00, 0x02, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
    {0x21, 0x08, 0x02, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
    {0x22, 0x20, 0x02, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
    {0x23, 0x20, 0x02, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
    {0x24, 0x00, 0x02, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
};

const uint8_t GRAY_PRE_BW[5][LUT_SIZE + 1] = {
    {0x20, 0x00, 0x06, 0x01, 0x06, 0x06, 0x01, 0x00, 0x02, 0x04, 0x00, 0x00, 0x01},
    {0x21, 0x20, 0x06, 0x01, 0x06, 0x06, 0x01, 0x00, 0x02, 0x04, 0x00, 0x00, 0x01},
    {0x22, 0xAA, 0x06, 0x01, 0x06, 0x06, 0x01, 0xA0, 0x02, 0x04, 0x00, 0x00, 0x01},
    {0x23, 0x55, 0x06, 0x01, 0x06, 0x06, 0x01, 0x50, 0x02, 0x04, 0x00, 0x00, 0x01},
    {0x24, 0x00, 0x06, 0x01, 0x06, 0x06, 0x01, 0x10, 0x02, 0x04, 0x00, 0x00, 0x01},
};

}  // namespace

Uc8179X4ProDriver::~Uc8179X4ProDriver() { releaseGrayBase(); }

bool Uc8179X4ProDriver::allocateGrayBase() {
  if (grayBase_ != nullptr) return true;
#ifdef TEST_BUILD
  grayBase_ = static_cast<uint8_t*>(std::malloc(BUFFER_SIZE));
#else
  constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  if (heap_caps_get_largest_free_block(caps) < BUFFER_SIZE) return false;
  grayBase_ = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, caps));
#endif
  return grayBase_ != nullptr;
}

void Uc8179X4ProDriver::releaseGrayBase() {
  if (grayBase_ == nullptr) return;
#ifdef TEST_BUILD
  std::free(grayBase_);
#else
  heap_caps_free(grayBase_);
#endif
  grayBase_ = nullptr;
}

void Uc8179X4ProDriver::initController(Uc8279Bus& bus) {
  const uint8_t panel[] = {0x3F, 0x0A};
  const uint8_t resolution[] = {0x03, 0x20, 0x02, 0x58};
  const uint8_t gateSource[] = {0x00, 0x00, 0x00, 0x00};
  const uint8_t powerOffSequence[] = {0x20};
  const uint8_t booster[] = {0x25, 0x25, 0x3C, 0x25};
  const uint8_t gateScan[] = {0x02};
  const uint8_t powerSave[] = {0x22};

  bus.commandData(CMD_PANEL_SETTING, panel, sizeof(panel));
  bus.commandData(CMD_RESOLUTION, resolution, sizeof(resolution));
  bus.commandData(CMD_GATE_SOURCE_START, gateSource, sizeof(gateSource));
  bus.commandData(CMD_POWER_OFF_SEQUENCE, powerOffSequence, sizeof(powerOffSequence));
  bus.commandData(CMD_BOOSTER_SOFT_START, booster, sizeof(booster));
  bus.commandData(CMD_GATE_SCAN, gateScan, sizeof(gateScan));
  bus.commandData(CMD_POWER_SAVE, powerSave, sizeof(powerSave));
}

bool Uc8179X4ProDriver::begin(Uc8279Bus& bus) {
  if (!allocateGrayBase()) return false;
  bus.reset(50);
  initController(bus);
  grayBaseValid_ = false;
  absoluteGrayPlanes_ = false;
  screenOn_ = false;
  needFullClear_ = true;
  oldPlaneValid_ = false;
  redriveAfterGray_ = false;
  lsbValid_ = false;
  msbValid_ = false;
  return true;
}

void Uc8179X4ProDriver::streamPlane(Uc8279Bus& bus, uint8_t command, const uint8_t* plane, bool invert) {
  uint8_t chunk[STREAM_CHUNK_SIZE];
  bus.beginData(command);
  for (int row = HEIGHT - 1; row >= 0; --row) {
    const uint8_t* source = plane + static_cast<uint32_t>(row) * WIDTH_BYTES;
    if (!invert) {
      bus.write(source, WIDTH_BYTES);
      continue;
    }
    for (size_t i = 0; i < WIDTH_BYTES; ++i) chunk[i] = static_cast<uint8_t>(~source[i]);
    bus.write(chunk, WIDTH_BYTES);
  }
  memset(chunk, 0xFF, WIDTH_BYTES);
  for (uint16_t row = HEIGHT; row < ADDRESSED_HEIGHT; ++row) bus.write(chunk, WIDTH_BYTES);
  bus.endData();
}

void Uc8179X4ProDriver::streamPlaneXor(Uc8279Bus& bus, uint8_t command, const uint8_t* lhs, const uint8_t* rhs) {
  uint8_t rowBuffer[WIDTH_BYTES];
  bus.beginData(command);
  for (int row = HEIGHT - 1; row >= 0; --row) {
    const uint32_t offset = static_cast<uint32_t>(row) * WIDTH_BYTES;
    for (size_t i = 0; i < WIDTH_BYTES; ++i) rowBuffer[i] = static_cast<uint8_t>(lhs[offset + i] ^ rhs[offset + i]);
    bus.write(rowBuffer, WIDTH_BYTES);
  }
  memset(rowBuffer, 0xFF, sizeof(rowBuffer));
  for (uint16_t row = HEIGHT; row < ADDRESSED_HEIGHT; ++row) bus.write(rowBuffer, WIDTH_BYTES);
  bus.endData();
}

void Uc8179X4ProDriver::fillPlane(Uc8279Bus& bus, uint8_t command, uint8_t value) {
  uint8_t row[WIDTH_BYTES];
  memset(row, value, sizeof(row));
  bus.beginData(command);
  for (uint16_t y = 0; y < ADDRESSED_HEIGHT; ++y) bus.write(row, sizeof(row));
  bus.endData();
}

void Uc8179X4ProDriver::invalidate() {
  oldPlaneValid_ = false;
  needFullClear_ = true;
  grayBaseValid_ = false;
  absoluteGrayPlanes_ = false;
  lsbValid_ = false;
  msbValid_ = false;
}

bool Uc8179X4ProDriver::powerOn(Uc8279Bus& bus, const char* operation) {
  if (screenOn_) return true;
  bus.command(CMD_POWER_ON);
  if (!bus.waitBusy(operation, true)) {
    screenOn_ = false;
    invalidate();
    return false;
  }
  screenOn_ = true;
  return true;
}

bool Uc8179X4ProDriver::transitionGrayscaleBase(Uc8279Bus& bus, const uint8_t* frame, bool turnOff) {
  if (!bus.waitReady("8179_gray_pre_ready")) {
    invalidate();
    return false;
  }

  streamPlane(bus, CMD_DTM2, frame);
  bus.command(CMD_PARTIAL_IN);
  const uint8_t window[] = {0x00, 0x00, 0x03, 0x1F, 0x00, 0x00, 0x01, 0xDF, 0x01};
  bus.commandData(CMD_PARTIAL_WINDOW, window, sizeof(window));
  const uint8_t panel[] = {0x3F, 0x0A};
  bus.commandData(CMD_PANEL_SETTING, panel, sizeof(panel));
  bus.command(CMD_POWER_OFF_SEQUENCE);
  bus.data(0x20);
  bus.command(CMD_GATE_SCAN);
  bus.data(0x02);
  const uint8_t activeCdi[] = {0x29, CDI_INTERVAL};
  bus.commandData(CMD_VCOM_DATA_INTERVAL, activeCdi, sizeof(activeCdi));
  bus.command(CMD_CCSET);
  bus.data(0x02);
  bus.command(CMD_TSSET);
  bus.data(0x5A);
  for (const auto& lut : GRAY_PRE_BW) bus.commandData(lut[0], lut + 1, LUT_SIZE);

  if (!powerOn(bus, "8179_gray_pre_PON")) return false;
  bus.command(CMD_DISPLAY_REFRESH);
  if (!bus.waitBusy("8179_gray_pre_DRF", true)) {
    invalidate();
    return false;
  }
  bus.command(CMD_PARTIAL_OUT);
  const uint8_t idleCdi[] = {0xA9, CDI_INTERVAL};
  bus.commandData(CMD_VCOM_DATA_INTERVAL, idleCdi, sizeof(idleCdi));
  streamPlane(bus, CMD_DTM1, frame);
  oldPlaneValid_ = true;
  needFullClear_ = false;
  redriveAfterGray_ = false;

  if (turnOff) {
    bus.command(CMD_POWER_OFF);
    if (!bus.waitBusy("8179_POF", true)) {
      invalidate();
      return false;
    }
    screenOn_ = false;
  }
  return true;
}

bool Uc8179X4ProDriver::display(Uc8279Bus& bus, const uint8_t* frame, Uc8179RefreshMode mode, bool turnOff) {
  if (frame == nullptr || grayBase_ == nullptr) return false;
  memcpy(grayBase_, frame, BUFFER_SIZE);
  grayBaseValid_ = true;
  absoluteGrayPlanes_ = false;
  lsbValid_ = false;
  msbValid_ = false;

  if (mode == Uc8179RefreshMode::Fast && redriveAfterGray_ && oldPlaneValid_ && !needFullClear_) {
    return transitionGrayscaleBase(bus, frame, turnOff);
  }

  const bool fast = mode == Uc8179RefreshMode::Fast && oldPlaneValid_ && !needFullClear_;
  streamPlane(bus, CMD_DTM2, frame);
  if (!fast) {
    if (mode == Uc8179RefreshMode::Half) {
      streamPlane(bus, CMD_DTM1, frame, true);
    } else {
      fillPlane(bus, CMD_DTM1, 0xFF);
    }
  }

  const uint8_t activeCdi[] = {0x29, CDI_INTERVAL};
  const uint8_t panel[] = {0x1F, 0x0A};
  bus.commandData(CMD_VCOM_DATA_INTERVAL, activeCdi, sizeof(activeCdi));
  bus.command(CMD_CCSET);
  bus.data(0x02);
  bus.command(CMD_TSSET);
  bus.data(fast ? 0x5A : 0x1E);
  bus.commandData(CMD_PANEL_SETTING, panel, sizeof(panel));
  if (fast) {
    bus.command(CMD_POWER_OFF_SEQUENCE);
    bus.data(0x20);
    bus.command(CMD_GATE_SCAN);
    bus.data(0x02);
  }

  if (!powerOn(bus, "8179_PON")) return false;
  if (fast) bus.command(CMD_PARTIAL_IN);
  bus.command(CMD_DISPLAY_REFRESH);
  if (!bus.waitBusy("8179_DRF", true)) {
    invalidate();
    return false;
  }
  if (fast) bus.command(CMD_PARTIAL_OUT);
  const uint8_t idleCdi[] = {0xA9, CDI_INTERVAL};
  bus.commandData(CMD_VCOM_DATA_INTERVAL, idleCdi, sizeof(idleCdi));
  streamPlane(bus, CMD_DTM1, frame);
  oldPlaneValid_ = true;
  needFullClear_ = false;
  redriveAfterGray_ = false;

  if (turnOff) {
    bus.command(CMD_POWER_OFF);
    if (!bus.waitBusy("8179_POF", true)) {
      invalidate();
      return false;
    }
    screenOn_ = false;
  }
  return true;
}

void Uc8179X4ProDriver::requestResync() { needFullClear_ = true; }

bool Uc8179X4ProDriver::copyGrayscaleLsb(Uc8279Bus& bus, const uint8_t* plane) {
  if (plane == nullptr || grayBase_ == nullptr || !grayBaseValid_) return false;
  if (!bus.waitReady("8179_gray_lsb")) {
    invalidate();
    return false;
  }
  for (uint32_t i = 0; i < BUFFER_SIZE; ++i) grayBase_[i] |= plane[i];
  streamPlane(bus, CMD_DTM1, grayBase_);
  // LSB overwrites DTM1, so the resident black-white old plane is gone.
  oldPlaneValid_ = false;
  grayBaseValid_ = false;
  absoluteGrayPlanes_ = true;
  lsbValid_ = true;
  msbValid_ = false;
  return true;
}

bool Uc8179X4ProDriver::copyGrayscaleMsb(Uc8279Bus& bus, const uint8_t* plane) {
  if (plane == nullptr || grayBase_ == nullptr || !absoluteGrayPlanes_ || !lsbValid_) return false;
  if (!bus.waitReady("8179_gray_msb")) {
    invalidate();
    return false;
  }
  streamPlaneXor(bus, CMD_DTM2, grayBase_, plane);
  for (uint32_t i = 0; i < BUFFER_SIZE; ++i) grayBase_[i] &= grayBase_[i] ^ plane[i];
  grayBaseValid_ = true;
  msbValid_ = true;
  return true;
}

bool Uc8179X4ProDriver::displayGray(Uc8279Bus& bus, bool turnOff) {
  if (!lsbValid_ || !msbValid_ || !grayBaseValid_) return false;
  if (!bus.waitReady("8179_gray_ready")) {
    invalidate();
    return false;
  }

  const uint8_t panel[] = {0x3F, 0x0A};
  bus.commandData(CMD_PANEL_SETTING, panel, sizeof(panel));
  for (const auto& lut : GRAY_LUTS) bus.commandData(lut[0], lut + 1, LUT_SIZE);
  const uint8_t activeCdi[] = {0x29, CDI_INTERVAL};
  bus.commandData(CMD_VCOM_DATA_INTERVAL, activeCdi, sizeof(activeCdi));

  if (!powerOn(bus, "8179_gray_PON")) return false;
  bus.command(CMD_DISPLAY_REFRESH);
  if (!bus.waitBusy("8179_gray_DRF", true)) {
    invalidate();
    return false;
  }
  streamPlane(bus, CMD_DTM1, grayBase_);
  streamPlane(bus, CMD_DTM2, grayBase_);
  oldPlaneValid_ = true;
  needFullClear_ = false;
  redriveAfterGray_ = true;
  lsbValid_ = false;
  msbValid_ = false;
  absoluteGrayPlanes_ = false;

  if (turnOff) {
    bus.command(CMD_POWER_OFF);
    if (!bus.waitBusy("8179_POF", true)) {
      invalidate();
      return false;
    }
    screenOn_ = false;
  }
  return true;
}

bool Uc8179X4ProDriver::cleanupGrayscale(Uc8279Bus& bus, const uint8_t* bwFrame) {
  if (bwFrame == nullptr) {
    invalidate();
    return false;
  }
  if (oldPlaneValid_) return true;
  if (!bus.waitReady("8179_gray_cleanup")) {
    invalidate();
    return false;
  }
  streamPlane(bus, CMD_DTM1, bwFrame);
  streamPlane(bus, CMD_DTM2, bwFrame);
  oldPlaneValid_ = true;
  needFullClear_ = false;
  return true;
}

bool Uc8179X4ProDriver::deepSleep(Uc8279Bus& bus) {
  if (screenOn_) {
    bus.command(CMD_POWER_OFF);
    if (!bus.waitBusy("8179_power_down", true)) {
      invalidate();
      return false;
    }
  }
  bus.command(CMD_DEEP_SLEEP);
  bus.data(0xA5);
  screenOn_ = false;
  invalidate();
  redriveAfterGray_ = false;
  return true;
}

Uc8179X4ProDriver& uc8179X4ProDriver() {
  static Uc8179X4ProDriver driver;
  return driver;
}

}  // namespace papyrix::eink

#endif
