#include "Ssd2677Driver.h"

#if defined(TEST_BUILD) || PAPYRIX_TARGET_STICKY
#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstring>

#include "Ssd2677Luts.h"

namespace papyrix::eink {
namespace {
// Adapted from FreeInk 683fe03009fdd03c725015d167da5362b01911c1. See FREEINK_LICENSE.
constexpr size_t kPlaneBytes = 48000;
constexpr uint32_t kCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
struct InitCommand {
  uint8_t command, size;
  bool wait;
  uint8_t data[8];
};
constexpr InitCommand kInit[] = {
    {0x00, 2, true, {0x2F, 0x0E}},
    {0x06, 4, false, {0x0F, 0x8B, 0x93, 0xC1}},
    {0xE7, 1, false, {0xC1}},
    {0x30, 1, false, {0x08}},
    {0x50, 1, false, {0x77}},
    {0x62, 8, false, {0x76, 0x76, 0x76, 0x5A, 0x9D, 0x8A, 0x76, 0x62}},
    {0x61, 4, false, {0x03, 0x20, 0x02, 0xA8}},
    {0xE0, 1, false, {0x10}},
    {0x65, 4, false, {0, 0, 0, 0}},
    {0xE9, 1, true, {0x01}},
};
constexpr uint8_t kSpread[] = {0, 1, 4, 5, 16, 17, 20, 21, 64, 65, 68, 69, 80, 81, 84, 85};
// shortcut: Fixed room-temperature waveforms, add temperature selection after device measurements.
constexpr uint8_t kFull = 0xEE, kFast = 0x19, kGray = 0xFA;
}  // namespace

Ssd2677Driver::~Ssd2677Driver() {
  heap_caps_free(lsb_);
  heap_caps_free(old_);
}

bool Ssd2677Driver::fail() {
  baselineValid_ = false;
  lsbValid_ = msbValid_ = graySucceeded_ = false;
  waveform_ = 0xFF;
  return false;
}

bool Ssd2677Driver::initPanel(Uc8279Bus& bus) {
  bus.reset(0);
  if (!bus.waitReady("2677_RESET")) return fail();
  for (const auto& item : kInit) {
    bus.commandData(item.command, item.data, item.size);
    if (item.wait && !bus.waitBusy("2677_INIT")) return fail();
  }
  if (!bus.waitReady("2677_INIT")) return fail();
  needsPowerOn_ = true;
  waveform_ = 0;
  return true;
}

Ssd2677InitResult Ssd2677Driver::begin(Uc8279Bus& bus) {
  initialized_ = false;
  baselineValid_ = lsbValid_ = msbValid_ = graySucceeded_ = false;
  if (!old_) {
    if (heap_caps_get_largest_free_block(kCaps) < kPlaneBytes) return Ssd2677InitResult::OutOfMemory;
    old_ = static_cast<uint8_t*>(heap_caps_malloc(kPlaneBytes, kCaps));
    if (!old_) return Ssd2677InitResult::OutOfMemory;
  }
  if (!initPanel(bus)) return Ssd2677InitResult::PanelError;
  initialized_ = true;
  return Ssd2677InitResult::Ok;
}

bool Ssd2677Driver::latchWaveform(Uc8279Bus& bus, uint8_t code) {
  if (waveform_ != 0 && waveform_ != code && !initPanel(bus)) return false;
  bus.command(0xE0);
  bus.data(0x12);
  bus.command(0xE6);
  bus.data(code);
  bus.command(0xA5);
  if (!bus.waitBusy("2677_LATCH")) return fail();
  delay(10);
  waveform_ = code;
  return true;
}

void Ssd2677Driver::sendPacked(Uc8279Bus& bus, const uint8_t* high, const uint8_t* low) {
  uint8_t row[200];
  bus.beginData(0x10);
  for (int y = 479; y >= 0; --y) {
    const size_t start = static_cast<size_t>(y) * 100;
    for (unsigned x = 0; x < 100; ++x) {
      const uint8_t hi = high[start + x], lo = low[start + x];
      row[2 * x] = static_cast<uint8_t>((kSpread[hi >> 4] << 1) | kSpread[lo >> 4]);
      row[2 * x + 1] = static_cast<uint8_t>((kSpread[hi & 15] << 1) | kSpread[lo & 15]);
    }
    bus.write(row, sizeof(row));
  }
  bus.endData();
}

bool Ssd2677Driver::powerOff(Uc8279Bus& bus) {
  if (needsPowerOn_) return true;
  bus.command(0x02);
  bus.data(0x00);
  if (!bus.waitBusy("2677_POF", true)) return fail();
  needsPowerOn_ = true;
  return true;
}

bool Ssd2677Driver::refresh(Uc8279Bus& bus, bool turnOff) {
  if (needsPowerOn_) {
    needsPowerOn_ = false;
    bus.command(0x04);
    if (!bus.waitBusy("2677_PON", true)) return fail();
  }
  bus.command(0x12);
  bus.data(0x00);
  if (!bus.waitBusy("2677_DRF", true)) return fail();
  return !turnOff || powerOff(bus);
}

bool Ssd2677Driver::display(Uc8279Bus& bus, const uint8_t* frame, Ssd2677RefreshMode mode, bool turnOff) {
  if (!initialized_ || !frame) return fail();
  lsbValid_ = msbValid_ = graySucceeded_ = false;
  const bool full = mode == Ssd2677RefreshMode::Full || !baselineValid_;
  if (!latchWaveform(bus, full ? kFull : kFast)) return false;
  sendPacked(bus, full ? frame : old_, frame);
  if (!refresh(bus, full || turnOff)) return false;
  memcpy(old_, frame, kPlaneBytes);
  baselineValid_ = true;
  return true;
}

void Ssd2677Driver::requestResync() { fail(); }

bool Ssd2677Driver::copyGrayscaleLsb(Uc8279Bus&, const uint8_t* plane, bool absolute) {
  lsbValid_ = msbValid_ = graySucceeded_ = false;
  if (!initialized_ || !plane || (!absolute && !baselineValid_)) return false;
  if (!lsb_) {
    if (heap_caps_get_largest_free_block(kCaps) < kPlaneBytes) return false;
    lsb_ = static_cast<uint8_t*>(heap_caps_malloc(kPlaneBytes, kCaps));
    if (!lsb_) return false;
  }
  memcpy(lsb_, plane, kPlaneBytes);
  absolute_ = absolute;
  lsbValid_ = true;
  return true;
}

bool Ssd2677Driver::copyGrayscaleMsb(Uc8279Bus& bus, const uint8_t* plane) {
  msbValid_ = false;
  if (!initialized_ || !lsbValid_ || !plane) return false;
  if (absolute_ ? !latchWaveform(bus, kGray) : !initPanel(bus)) return false;
  sendPacked(bus, plane, lsb_);
  msbValid_ = true;
  return true;
}

bool Ssd2677Driver::displayGray(Uc8279Bus& bus, bool turnOff) {
  if (!initialized_ || !lsbValid_ || !msbValid_) return false;
  if (!absolute_) bus.commandData(0x20, kStickySsd2677AaLut, sizeof(kStickySsd2677AaLut));
  graySucceeded_ = refresh(bus, absolute_ || turnOff);
  if (absolute_) baselineValid_ = false;
  if (!absolute_ && graySucceeded_) waveform_ = 0xFF;
  return graySucceeded_;
}

bool Ssd2677Driver::cleanupGrayscale(Uc8279Bus&, const uint8_t* bwFrame) {
  const bool ok = graySucceeded_ && bwFrame != nullptr;
  heap_caps_free(lsb_);
  lsb_ = nullptr;
  lsbValid_ = msbValid_ = graySucceeded_ = false;
  return ok;
}

bool Ssd2677Driver::deepSleep(Uc8279Bus& bus) {
  if (!initialized_ || !powerOff(bus)) return false;
  bus.command(0x07);
  bus.data(0xA5);
  initialized_ = false;
  baselineValid_ = false;
  waveform_ = 0;
  heap_caps_free(lsb_);
  lsb_ = nullptr;
  lsbValid_ = msbValid_ = graySucceeded_ = false;
  return true;
}

Ssd2677Driver& ssd2677Driver() {
  static Ssd2677Driver driver;
  return driver;
}
}  // namespace papyrix::eink
#endif
