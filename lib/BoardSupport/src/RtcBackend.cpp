#include "RtcBackend.h"

#include <HardwareIdentity.h>
#include <I2cBus.h>
#include <TargetConfig.h>
#include <Wire.h>
#if PAPYRIX_TARGET_MURPHY_M4
#include <Arduino.h>
#include <MurphyM4I2c.h>
#endif

namespace papyrix::board {
namespace {

bool readRegisters(const RtcConfig& config, uint8_t reg, uint8_t* data, uint8_t size) {
#if PAPYRIX_TARGET_MURPHY_M4
  if (config.type == RtcType::Rx8010) return murphy_m4_i2c::read(murphy_m4_i2c::Device::Rtc, reg, data, size);
#endif
  auto& wire = i2cBus(config.busIndex);
  wire.begin(config.sda, config.scl, config.i2cHz);
  wire.setTimeOut(10);
  wire.beginTransmission(config.i2cAddress);
  wire.write(reg);
  if (wire.endTransmission() != 0 || wire.requestFrom(config.i2cAddress, size, uint8_t{1}) < size) return false;
  for (uint8_t i = 0; i < size; ++i) data[i] = wire.read();
  return true;
}

bool writeRegister(const RtcConfig& config, uint8_t reg, uint8_t value) {
#if PAPYRIX_TARGET_MURPHY_M4
  if (config.type == RtcType::Rx8010) {
    const uint8_t data[]{reg, value};
    return murphy_m4_i2c::write(murphy_m4_i2c::Device::Rtc, data, sizeof(data));
  }
#endif
  auto& wire = i2cBus(config.busIndex);
  wire.begin(config.sda, config.scl, config.i2cHz);
  wire.setTimeOut(10);
  wire.beginTransmission(config.i2cAddress);
  wire.write(reg);
  wire.write(value);
  return wire.endTransmission() == 0;
}

#if PAPYRIX_TARGET_MURPHY_M4
bool validRx8010Time(const DateTime& value) {
  if (value.year < 2000 || value.year > 2099 || value.month < 1 || value.month > 12 || value.day < 1 ||
      value.hour > 23 || value.minute > 59 || value.second > 59 || value.weekday > 6)
    return false;
  constexpr uint8_t days[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const uint8_t limit = value.month == 2 && value.year % 4 == 0 ? 29 : days[value.month - 1];
  return value.day <= limit;
}

bool initializeRx8010(const RtcConfig& config) {
  static constexpr uint8_t alarmsAndTimer[]{0x17, 0xD8, 0, 0, 0, 0, 0};
  static constexpr uint8_t offsetAndIrq[]{0x30, 0, 0x08, 0};
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    if (!writeRegister(config, 0x1F, 0x40) || !writeRegister(config, 0x1D, 0x04)) return false;
    if (!murphy_m4_i2c::write(murphy_m4_i2c::Device::Rtc, alarmsAndTimer, sizeof(alarmsAndTimer)) ||
        !murphy_m4_i2c::write(murphy_m4_i2c::Device::Rtc, offsetAndIrq, sizeof(offsetAndIrq)) ||
        !writeRegister(config, 0x1E, 0))
      return false;
    uint8_t flags = 0;
    if (!readRegisters(config, 0x1E, &flags, 1)) return false;
    if (!(flags & 0x02)) return true;
    if (attempt == 0) delay(3000);
  }
  return false;
}

bool readRx8010(const RtcConfig& config, DateTime& out) {
  uint8_t status[2]{}, raw[7]{};
  if (!readRegisters(config, 0x1E, status, sizeof(status)) || (status[0] & 0x02) || (status[1] & 0x40) ||
      !readRegisters(config, 0x10, raw, sizeof(raw)))
    return false;
  for (uint8_t i = 0; i < sizeof(raw); ++i) {
    if (i != 3 && ((raw[i] & 0x0F) > 9 || (raw[i] >> 4) > 9)) return false;
  }
  const uint8_t weekday = raw[3];
  if (!weekday || (weekday & 0x80) || (weekday & (weekday - 1))) return false;
  DateTime value{};
  value.second = bcdToDecimal(raw[0]);
  value.minute = bcdToDecimal(raw[1]);
  value.hour = bcdToDecimal(raw[2]);
  while ((weekday >> value.weekday) != 1) ++value.weekday;
  value.day = bcdToDecimal(raw[4]);
  value.month = bcdToDecimal(raw[5]);
  value.year = static_cast<uint16_t>(2000 + bcdToDecimal(raw[6]));
  if (!validRx8010Time(value)) return false;
  out = value;
  return true;
}
#endif

}  // namespace

bool RtcBackend::begin() {
  const auto& config = HardwareIdentity::instance().profile().rtc;
  if (config.type == RtcType::None || config.sda == kPinUnused || config.scl == kPinUnused) return false;
#if PAPYRIX_TARGET_MURPHY_M4
  if (config.type == RtcType::Rx8010) {
    uint8_t flags = 0;
    available_ = murphy_m4_i2c::begin() && readRegisters(config, 0x1E, &flags, 1) &&
                 (!(flags & 0x02) || initializeRx8010(config));
    return available_;
  }
#endif
  uint8_t status = 0;
  const uint8_t reg = config.type == RtcType::Ds3231 ? 0x0F : 0x00;
  available_ = readRegisters(config, reg, &status, 1);
  if (available_ && config.type == RtcType::Ds3231) writeRegister(config, 0x0E, 0x04);
  if (available_ && (config.type == RtcType::Bm8563 || config.type == RtcType::Pcf8563))
    writeRegister(config, 0x0D, 0x00);
  return available_;
}

bool RtcBackend::read(DateTime& out) {
  if (!available_) return false;
  const auto& config = HardwareIdentity::instance().profile().rtc;
#if PAPYRIX_TARGET_MURPHY_M4
  if (config.type == RtcType::Rx8010) return readRx8010(config, out);
#endif
  uint8_t raw[7]{};
  const uint8_t reg = config.type == RtcType::Ds3231 ? 0x00 : 0x02;
  if (!readRegisters(config, reg, raw, sizeof(raw))) return false;
  if ((config.type == RtcType::Bm8563 || config.type == RtcType::Pcf8563) && (raw[0] & 0x80)) return false;
  if (config.type == RtcType::Ds3231) {
    uint8_t status = 0;
    if (!readRegisters(config, 0x0F, &status, 1) || (status & 0x80)) return false;
  }
  out.second = bcdToDecimal(raw[0] & 0x7F);
  out.minute = bcdToDecimal(raw[1] & 0x7F);
  out.hour = bcdToDecimal(raw[2] & 0x3F);
  if (config.type == RtcType::Ds3231) {
    out.weekday = bcdToDecimal(raw[3] & 0x07) % 7;
    out.day = bcdToDecimal(raw[4] & 0x3F);
  } else {
    out.day = bcdToDecimal(raw[3] & 0x3F);
    out.weekday = raw[4] & 0x07;
  }
  out.month = bcdToDecimal(raw[5] & 0x1F);
  out.year = static_cast<uint16_t>(2000 + bcdToDecimal(raw[6]));
  return true;
}

bool RtcBackend::write(const DateTime& value) {
  if (!available_) return false;
  const auto& config = HardwareIdentity::instance().profile().rtc;
#if PAPYRIX_TARGET_MURPHY_M4
  if (config.type == RtcType::Rx8010) {
    if (!validRx8010Time(value)) return false;
    uint8_t flags = 0;
    if (!readRegisters(config, 0x1E, &flags, 1)) return false;
    if (flags & 0x02) {
      if (!initializeRx8010(config)) return false;
    } else if (!writeRegister(config, 0x1F, 0x40)) {
      return false;
    }
    const uint8_t data[]{0x10,
                         decimalToBcd(value.second),
                         decimalToBcd(value.minute),
                         decimalToBcd(value.hour),
                         static_cast<uint8_t>(1u << value.weekday),
                         decimalToBcd(value.day),
                         decimalToBcd(value.month),
                         decimalToBcd(static_cast<uint8_t>(value.year - 2000))};
    if (!murphy_m4_i2c::write(murphy_m4_i2c::Device::Rtc, data, sizeof(data))) return false;
    if (!readRegisters(config, 0x1E, &flags, 1) || (flags & 0x02)) return false;
    return writeRegister(config, 0x1F, 0);
  }
#endif
  const uint8_t reg = config.type == RtcType::Ds3231 ? 0x00 : 0x02;
  auto& wire = i2cBus(config.busIndex);
  wire.begin(config.sda, config.scl, config.i2cHz);
  wire.setTimeOut(10);
  wire.beginTransmission(config.i2cAddress);
  wire.write(reg);
  wire.write(decimalToBcd(value.second));
  wire.write(decimalToBcd(value.minute));
  wire.write(decimalToBcd(value.hour));
  if (config.type == RtcType::Bm8563 || config.type == RtcType::Pcf8563) {
    wire.write(decimalToBcd(value.day));
    wire.write(decimalToBcd(value.weekday));
  } else {
    wire.write(decimalToBcd(value.weekday == 0 ? 7 : value.weekday));
    wire.write(decimalToBcd(value.day));
  }
  wire.write(decimalToBcd(value.month));
  wire.write(decimalToBcd(static_cast<uint8_t>(value.year % 100)));
  if (wire.endTransmission() != 0) return false;
  if (config.type == RtcType::Ds3231) {
    uint8_t status = 0;
    if (!readRegisters(config, 0x0F, &status, 1)) return false;
    return writeRegister(config, 0x0F, static_cast<uint8_t>(status & ~0x80));
  }
  return true;
}

}  // namespace papyrix::board
