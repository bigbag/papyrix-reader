#include "Uc8279X4ProDriver.h"

#if defined(TEST_BUILD) || PAPYRIX_TARGET_X4PRO || PAPYRIX_TARGET_X4CLASSIC

// This driver selectively ports the UC8279 X4 Pro path from FreeInk SDK
// revision 6fabbec80c4d0d7cb6654046caef367a3c750c36. The license is in
// ../FREEINK_LICENSE.

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
constexpr uint8_t CMD_DEEP_SLEEP = 0x07;
constexpr uint8_t CMD_DTM1 = 0x10;
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;
constexpr uint8_t CMD_DTM2 = 0x13;
constexpr uint8_t CMD_PLL = 0x30;
constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;
constexpr uint8_t CMD_RESOLUTION = 0x61;
constexpr uint8_t CMD_GATE_SOURCE_START = 0x65;
constexpr uint8_t CMD_PARTIAL_WINDOW = 0x90;
constexpr uint8_t CMD_PARTIAL_IN = 0x91;
constexpr uint8_t CMD_PARTIAL_OUT = 0x92;
constexpr uint8_t CMD_CCSET = 0xE0;
constexpr uint8_t CMD_GATE_SCAN = 0xE1;
constexpr uint8_t CMD_TSSET = 0xE5;
constexpr size_t GRAY_LUT_SIZE = 49;
constexpr size_t PRECONDITION_LUT_SIZE = 42;
constexpr size_t STREAM_CHUNK_SIZE = 128;

const uint8_t GRAY_LUTS_02[5][GRAY_LUT_SIZE + 1] = {
    {0x20, 0x01, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x21, 0x01, 0x02, 0x02, 0x41, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x22, 0x01, 0x02, 0x82, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x23, 0x01, 0x02, 0x82, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x24, 0x01, 0x02, 0x02, 0x81, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
};

const uint8_t GRAY_LUTS_68[5][GRAY_LUT_SIZE + 1] = {
    {0x20, 0x01, 0x02, 0x03, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x21, 0x01, 0x02, 0x03, 0x41, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x22, 0x01, 0x02, 0x83, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x23, 0x01, 0x02, 0x83, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
    {0x24, 0x01, 0x02, 0x03, 0x81, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01},
};

const uint8_t GRAY_PRE_BW[5][PRECONDITION_LUT_SIZE + 1] = {
    {0x20, 0x01, 0x06, 0x01, 0x06, 0x06, 0x01, 0x01, 0x01, 0x02, 0x04, 0x00, 0x00, 0x01, 0x01},
    {0x21, 0x01, 0x06, 0x81, 0x06, 0x06, 0x01, 0x01, 0x01, 0x02, 0x04, 0x00, 0x00, 0x01, 0x01},
    {0x22, 0x01, 0x86, 0x81, 0x86, 0x86, 0x01, 0x01, 0x01, 0x82, 0x84, 0x00, 0x00, 0x01, 0x01},
    {0x23, 0x01, 0x46, 0x41, 0x46, 0x46, 0x01, 0x01, 0x01, 0x42, 0x44, 0x00, 0x00, 0x01, 0x01},
    {0x24, 0x01, 0x06, 0x01, 0x06, 0x06, 0x01, 0x01, 0x01, 0x02, 0x44, 0x00, 0x00, 0x01, 0x01},
};

}  // namespace

Uc8279X4ProDriver::~Uc8279X4ProDriver() { releaseGrayBase(); }

bool Uc8279X4ProDriver::allocateGrayBase() {
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

void Uc8279X4ProDriver::releaseGrayBase() {
  if (grayBase_ == nullptr) return;
#ifdef TEST_BUILD
  std::free(grayBase_);
#else
  heap_caps_free(grayBase_);
#endif
  grayBase_ = nullptr;
}

void Uc8279X4ProDriver::initController(Uc8279Bus& bus) {
  const uint8_t panel[] = {0x37, 0x4D};
  const uint8_t resolution[] = {0x03, 0x20, 0x02, 0x58};
  const uint8_t gateSource[] = {0x00, 0x00, 0x00, 0x00};
  const uint8_t powerOffSequence[] = {0x20};
  const uint8_t pll[] = {0x0E};
  const uint8_t gateScan[] = {0x02};

  bus.commandData(CMD_PANEL_SETTING, panel, sizeof(panel));
  bus.commandData(CMD_RESOLUTION, resolution, sizeof(resolution));
  bus.commandData(CMD_GATE_SOURCE_START, gateSource, sizeof(gateSource));
  bus.commandData(CMD_POWER_OFF_SEQUENCE, powerOffSequence, sizeof(powerOffSequence));
  if (programPll_) bus.commandData(CMD_PLL, pll, sizeof(pll));
  bus.commandData(CMD_GATE_SCAN, gateScan, sizeof(gateScan));
}

bool Uc8279X4ProDriver::begin(Uc8279Bus& bus, uint8_t lutVersion, bool programPll) {
  lutVersion_ = lutVersion;
  programPll_ = programPll;
  if (supportsGrayscale()) {
    if (!allocateGrayBase()) return false;
  } else {
    releaseGrayBase();
  }
  bus.reset(50);
  initController(bus);
  grayBaseValid_ = false;
  absoluteGrayPlanes_ = false;
  screenOn_ = false;
  needFullClear_ = true;
  oldPlaneValid_ = false;
  grayRefreshedOnce_ = false;
  redriveAfterGray_ = false;
  lsbValid_ = false;
  msbValid_ = false;
  return true;
}

void Uc8279X4ProDriver::streamPlane(Uc8279Bus& bus, uint8_t command, const uint8_t* plane, bool invert) {
  uint8_t rowBuffer[STREAM_CHUNK_SIZE];
  memset(rowBuffer, 0xFF, WIDTH_BYTES);
  bus.beginData(command);
  for (uint16_t row = 0; row < GATE_OFFSET; ++row) bus.write(rowBuffer, WIDTH_BYTES);
  for (uint16_t row = 0; row < HEIGHT; ++row) {
    const uint8_t* source = plane + static_cast<uint32_t>(row) * WIDTH_BYTES;
    if (!invert) {
      bus.write(source, WIDTH_BYTES);
      continue;
    }
    for (size_t i = 0; i < WIDTH_BYTES; ++i) rowBuffer[i] = static_cast<uint8_t>(~source[i]);
    bus.write(rowBuffer, WIDTH_BYTES);
  }
  memset(rowBuffer, 0xFF, WIDTH_BYTES);
  for (uint16_t row = GATE_OFFSET + HEIGHT; row < ADDRESSED_HEIGHT; ++row) bus.write(rowBuffer, WIDTH_BYTES);
  bus.endData();
}

void Uc8279X4ProDriver::streamPlaneXor(Uc8279Bus& bus, uint8_t command, const uint8_t* lhs, const uint8_t* rhs,
                                       bool invert) {
  uint8_t rowBuffer[STREAM_CHUNK_SIZE];
  memset(rowBuffer, 0xFF, WIDTH_BYTES);
  bus.beginData(command);
  for (uint16_t row = 0; row < GATE_OFFSET; ++row) bus.write(rowBuffer, WIDTH_BYTES);
  for (uint16_t row = 0; row < HEIGHT; ++row) {
    const uint32_t offset = static_cast<uint32_t>(row) * WIDTH_BYTES;
    for (size_t i = 0; i < WIDTH_BYTES; ++i) {
      const uint8_t value = static_cast<uint8_t>(lhs[offset + i] ^ rhs[offset + i]);
      rowBuffer[i] = invert ? static_cast<uint8_t>(~value) : value;
    }
    bus.write(rowBuffer, WIDTH_BYTES);
  }
  memset(rowBuffer, 0xFF, WIDTH_BYTES);
  for (uint16_t row = GATE_OFFSET + HEIGHT; row < ADDRESSED_HEIGHT; ++row) bus.write(rowBuffer, WIDTH_BYTES);
  bus.endData();
}

void Uc8279X4ProDriver::fillPlane(Uc8279Bus& bus, uint8_t command, uint8_t value) {
  uint8_t row[WIDTH_BYTES];
  memset(row, value, sizeof(row));
  bus.beginData(command);
  for (uint16_t y = 0; y < ADDRESSED_HEIGHT; ++y) bus.write(row, sizeof(row));
  bus.endData();
}

void Uc8279X4ProDriver::writePartialWindow(Uc8279Bus& bus) {
  const uint8_t window[] = {0x00, 0x00, 0x03, 0x1F, 0x00, 0x78, 0x02, 0x57, 0x01};
  bus.commandData(CMD_PARTIAL_WINDOW, window, sizeof(window));
}

void Uc8279X4ProDriver::invalidate() {
  oldPlaneValid_ = false;
  needFullClear_ = true;
  grayBaseValid_ = false;
  absoluteGrayPlanes_ = false;
  lsbValid_ = false;
  msbValid_ = false;
}

bool Uc8279X4ProDriver::powerOn(Uc8279Bus& bus, const char* operation) {
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

bool Uc8279X4ProDriver::transitionGrayscaleBase(Uc8279Bus& bus, const uint8_t* frame, bool turnOff) {
  if (!bus.waitReady("8279x4_gray_pre_ready")) {
    invalidate();
    return false;
  }

  streamPlane(bus, CMD_DTM2, frame);
  bus.command(CMD_PARTIAL_IN);
  writePartialWindow(bus);
  const uint8_t panel[] = {0x37, 0x4D};
  bus.commandData(CMD_PANEL_SETTING, panel, sizeof(panel));
  bus.command(CMD_POWER_OFF_SEQUENCE);
  bus.data(0x20);
  bus.command(CMD_GATE_SCAN);
  bus.data(0x02);
  bus.command(CMD_VCOM_DATA_INTERVAL);
  bus.data(0xD7);
  bus.command(CMD_CCSET);
  bus.data(0x02);
  bus.command(CMD_TSSET);
  bus.data(0x5A);
  for (const auto& lut : GRAY_PRE_BW) bus.commandData(lut[0], lut + 1, PRECONDITION_LUT_SIZE);

  if (!powerOn(bus, "8279x4_gray_pre_PON")) return false;
  bus.command(CMD_DISPLAY_REFRESH);
  if (!bus.waitBusy("8279x4_gray_pre_DRF", true)) {
    invalidate();
    return false;
  }
  bus.command(CMD_PARTIAL_OUT);
  streamPlane(bus, CMD_DTM1, frame);
  oldPlaneValid_ = true;
  needFullClear_ = false;
  redriveAfterGray_ = false;

  if (turnOff) {
    bus.command(CMD_POWER_OFF);
    if (!bus.waitBusy("8279x4_POF", true)) {
      invalidate();
      return false;
    }
    screenOn_ = false;
  }
  return true;
}

bool Uc8279X4ProDriver::display(Uc8279Bus& bus, const uint8_t* frame, Uc8279X4RefreshMode mode, bool turnOff) {
  if (frame == nullptr) return false;
  if (grayBase_ != nullptr) memcpy(grayBase_, frame, BUFFER_SIZE);
  grayBaseValid_ = grayBase_ != nullptr;
  absoluteGrayPlanes_ = false;
  lsbValid_ = false;
  msbValid_ = false;

  if (mode == Uc8279X4RefreshMode::Fast && redriveAfterGray_ && grayRefreshedOnce_ && oldPlaneValid_ &&
      !needFullClear_) {
    return transitionGrayscaleBase(bus, frame, turnOff);
  }

  const bool fast = mode == Uc8279X4RefreshMode::Fast && oldPlaneValid_ && !needFullClear_;
  streamPlane(bus, CMD_DTM2, frame);
  if (!fast) {
    if (mode == Uc8279X4RefreshMode::Half) {
      streamPlane(bus, CMD_DTM1, frame, true);
    } else {
      fillPlane(bus, CMD_DTM1, 0xFF);
    }
  }
  redriveAfterGray_ = false;

  bus.command(CMD_VCOM_DATA_INTERVAL);
  bus.data(fast ? 0xD7 : 0x97);
  bus.command(CMD_CCSET);
  bus.data(0x02);
  bus.command(CMD_TSSET);
  bus.data(fast ? 0x5A : 0x1E);
  if (fast) {
    bus.command(CMD_POWER_OFF_SEQUENCE);
    bus.data(0x20);
    bus.command(CMD_GATE_SCAN);
    bus.data(0x02);
  }

  if (!powerOn(bus, "8279x4_PON")) return false;
  if (fast) {
    bus.command(CMD_PARTIAL_IN);
    writePartialWindow(bus);
  }
  const uint8_t otpPanel[] = {0x17, 0x4D};
  bus.commandData(CMD_PANEL_SETTING, otpPanel, sizeof(otpPanel));
  bus.command(CMD_DISPLAY_REFRESH);
  if (!bus.waitBusy("8279x4_DRF", true)) {
    invalidate();
    return false;
  }
  if (fast) bus.command(CMD_PARTIAL_OUT);
  streamPlane(bus, CMD_DTM1, frame);
  oldPlaneValid_ = true;
  needFullClear_ = false;

  if (turnOff) {
    bus.command(CMD_POWER_OFF);
    if (!bus.waitBusy("8279x4_POF", true)) {
      invalidate();
      return false;
    }
    screenOn_ = false;
  }
  return true;
}

void Uc8279X4ProDriver::requestResync() { needFullClear_ = true; }

bool Uc8279X4ProDriver::copyGrayscaleLsb(Uc8279Bus& bus, const uint8_t* plane) {
  if (plane == nullptr || grayBase_ == nullptr || !grayBaseValid_) return false;
  if (!bus.waitReady("8279x4_gray_lsb")) {
    invalidate();
    return false;
  }
  for (uint32_t i = 0; i < BUFFER_SIZE; ++i) grayBase_[i] |= plane[i];
  streamPlane(bus, CMD_DTM1, grayBase_, true);
  // LSB overwrites DTM1, so the resident black-white old plane is gone.
  oldPlaneValid_ = false;
  grayBaseValid_ = false;
  absoluteGrayPlanes_ = true;
  lsbValid_ = true;
  msbValid_ = false;
  return true;
}

bool Uc8279X4ProDriver::copyGrayscaleMsb(Uc8279Bus& bus, const uint8_t* plane) {
  if (plane == nullptr || grayBase_ == nullptr || !absoluteGrayPlanes_ || !lsbValid_) return false;
  if (!bus.waitReady("8279x4_gray_msb")) {
    invalidate();
    return false;
  }
  streamPlaneXor(bus, CMD_DTM2, grayBase_, plane, true);
  for (uint32_t i = 0; i < BUFFER_SIZE; ++i) grayBase_[i] &= grayBase_[i] ^ plane[i];
  grayBaseValid_ = true;
  msbValid_ = true;
  return true;
}

bool Uc8279X4ProDriver::displayGray(Uc8279Bus& bus, bool turnOff) {
  if (!supportsGrayscale()) return false;
  if (!lsbValid_ || !msbValid_ || !grayBaseValid_) return false;
  if (!bus.waitReady("8279x4_gray_ready")) {
    invalidate();
    return false;
  }

  const uint8_t panel[] = {0x37, 0x4D};
  bus.commandData(CMD_PANEL_SETTING, panel, sizeof(panel));
  const auto& grayLuts = (lutVersion_ == 0x02 || (!programPll_ && lutVersion_ == 0x03)) ? GRAY_LUTS_02 : GRAY_LUTS_68;
  for (const auto& lut : grayLuts) bus.commandData(lut[0], lut + 1, GRAY_LUT_SIZE);
  bus.command(CMD_VCOM_DATA_INTERVAL);
  bus.data(0x97);
  grayRefreshedOnce_ = true;

  if (!powerOn(bus, "8279x4_gray_PON")) return false;
  bus.commandData(CMD_PANEL_SETTING, panel, sizeof(panel));
  bus.command(CMD_DISPLAY_REFRESH);
  if (!bus.waitBusy("8279x4_gray_DRF", true)) {
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
    if (!bus.waitBusy("8279x4_POF", true)) {
      invalidate();
      return false;
    }
    screenOn_ = false;
  }
  return true;
}

bool Uc8279X4ProDriver::cleanupGrayscale(Uc8279Bus& bus, const uint8_t* bwFrame) {
  if (bwFrame == nullptr) {
    invalidate();
    return false;
  }
  if (oldPlaneValid_) return true;
  if (!bus.waitReady("8279x4_gray_cleanup")) {
    invalidate();
    return false;
  }
  streamPlane(bus, CMD_DTM1, bwFrame);
  oldPlaneValid_ = true;
  needFullClear_ = false;
  return true;
}

bool Uc8279X4ProDriver::deepSleep(Uc8279Bus& bus) {
  if (screenOn_) {
    bus.command(CMD_POWER_OFF);
    if (!bus.waitBusy("8279x4_power_down", true)) {
      invalidate();
      return false;
    }
  }
  bus.command(CMD_DEEP_SLEEP);
  bus.data(0xA5);
  screenOn_ = false;
  invalidate();
  grayRefreshedOnce_ = false;
  redriveAfterGray_ = false;
  return true;
}

Uc8279X4ProDriver& uc8279X4ProDriver() {
  static Uc8279X4ProDriver driver;
  return driver;
}

}  // namespace papyrix::eink

#endif
