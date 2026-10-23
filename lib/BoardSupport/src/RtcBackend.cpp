#include "RtcBackend.h"

#include <HardwareIdentity.h>
#include <I2cBus.h>
#include <Wire.h>

namespace papyrix::board {
namespace {

bool readRegisters(const RtcConfig& config, uint8_t reg, uint8_t* data, uint8_t size) {
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
  auto& wire = i2cBus(config.busIndex);
  wire.begin(config.sda, config.scl, config.i2cHz);
  wire.setTimeOut(10);
  wire.beginTransmission(config.i2cAddress);
  wire.write(reg);
  wire.write(value);
  return wire.endTransmission() == 0;
}

}  // namespace

bool RtcBackend::begin() {
  const auto& config = HardwareIdentity::instance().profile().rtc;
  if (config.type == RtcType::None || config.sda == kPinUnused || config.scl == kPinUnused) return false;
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
