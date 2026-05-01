#include "BatteryMonitor.h"

#include <Arduino.h>
#include <Wire.h>

inline float min(const float a, const float b) { return a < b ? a : b; }
inline float max(const float a, const float b) { return a > b ? a : b; }

namespace {
constexpr uint8_t I2C_ADDR_BQ27220 = 0x55;
constexpr uint8_t BQ27220_SOC_REG = 0x2C;   // 16-bit LE, percentage
constexpr uint8_t BQ27220_VOLT_REG = 0x08;  // 16-bit LE, mV
constexpr uint8_t BQ27220_CUR_REG = 0x0C;   // 16-bit LE, signed mA (positive = charging)
constexpr uint16_t BQ27220_TIMEOUT_MS = 6;

bool readBq27220Reg16Le(uint8_t reg, uint16_t* out) {
  Wire.beginTransmission(I2C_ADDR_BQ27220);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<int>(I2C_ADDR_BQ27220), 2) != 2) return false;
  if (Wire.available() < 2) return false;
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *out = (static_cast<uint16_t>(hi) << 8) | lo;
  return true;
}
}  // namespace

BatteryMonitor::BatteryMonitor(uint8_t adcPin, float dividerMultiplier)
    : _mode(Mode::Adc), _adcPin(adcPin), _dividerMultiplier(dividerMultiplier) {}

BatteryMonitor::BatteryMonitor(const Bq27220Config& cfg) : _mode(Mode::Bq27220), _i2c(cfg) {}

uint16_t BatteryMonitor::readBq27220Soc_() const {
  const unsigned long now = millis();
  if (_haveBqReading && _lastSocPollMs != 0 && (now - _lastSocPollMs) < kBqPollIntervalMs) {
    return _lastGoodSoc;
  }

  Wire.begin(_i2c.sdaPin, _i2c.sclPin, _i2c.freq);
  Wire.setTimeOut(BQ27220_TIMEOUT_MS);
  uint16_t soc = 0;
  const bool ok = readBq27220Reg16Le(BQ27220_SOC_REG, &soc);
  Wire.end();
  // Hand pins back so digitalRead(UART0_RXD=20) for USB detection still works.
  pinMode(_i2c.sdaPin, INPUT);
  pinMode(_i2c.sclPin, INPUT);

  _lastSocPollMs = now;
  if (!ok || soc > 100) {
    // Pre-first-success fallback is 100, not 0: a transient I²C glitch on the
    // very first read otherwise seeds the EMA in readSmoothedPercentage() at 0%
    // and trips low-battery UI. A fully-discharged battery wouldn't have booted
    // the device anyway, so 100 is the safer default until we get real data.
    return _haveBqReading ? _lastGoodSoc : 100;
  }
  _lastGoodSoc = soc;
  _haveBqReading = true;
  return soc;
}

uint16_t BatteryMonitor::readBq27220Mv_() const {
  const unsigned long now = millis();
  if (_haveBqReading && _lastMvPollMs != 0 && (now - _lastMvPollMs) < kBqPollIntervalMs) {
    return _lastGoodMv;
  }

  Wire.begin(_i2c.sdaPin, _i2c.sclPin, _i2c.freq);
  Wire.setTimeOut(BQ27220_TIMEOUT_MS);
  uint16_t mv = 0;
  const bool ok = readBq27220Reg16Le(BQ27220_VOLT_REG, &mv);
  Wire.end();
  pinMode(_i2c.sdaPin, INPUT);
  pinMode(_i2c.sclPin, INPUT);

  _lastMvPollMs = now;
  if (!ok || mv < 2500 || mv > 5000) {
    // Mirror the SoC fallback: pre-first-success returns a typical full-charge
    // voltage rather than 0, so debug screens and any voltage→percent fallback
    // in callers don't show "dead battery" on a transient I²C glitch.
    return _haveBqReading ? _lastGoodMv : 4100;
  }
  _lastGoodMv = mv;
  _haveBqReading = true;
  return mv;
}

uint16_t BatteryMonitor::readPercentage() const {
  if (_mode == Mode::Bq27220) {
    return readBq27220Soc_();
  }
  return percentageFromMillivolts(readMillivolts());
}

uint16_t BatteryMonitor::readSmoothedPercentage() const {
  const uint16_t raw = readPercentage();
  if (!_smoothInitialized) {
    _smoothedScaled = raw * 10;
    _smoothInitialized = true;
  } else {
    _smoothedScaled = (_smoothedScaled * 9 + raw * 10) / 10;
  }
  return _smoothedScaled / 10;
}

uint16_t BatteryMonitor::readMillivolts() const {
  if (_mode == Mode::Bq27220) {
    return readBq27220Mv_();
  }
  const uint16_t mv = readRawMillivolts();
  return static_cast<uint16_t>(mv * _dividerMultiplier);
}

uint16_t BatteryMonitor::readRawMillivolts() const {
  if (_mode == Mode::Bq27220) {
    return readBq27220Mv_();
  }
  return analogReadMilliVolts(_adcPin);
}

double BatteryMonitor::readVolts() const { return static_cast<double>(readMillivolts()) / 1000.0; }

bool BatteryMonitor::readBq27220Current_(int16_t* outMa) const {
  const unsigned long now = millis();
  if (_haveBqCurrent && _lastCurrentPollMs != 0 && (now - _lastCurrentPollMs) < kBqPollIntervalMs) {
    *outMa = _lastGoodCurrentMa;
    return true;
  }

  Wire.begin(_i2c.sdaPin, _i2c.sclPin, _i2c.freq);
  Wire.setTimeOut(BQ27220_TIMEOUT_MS);
  uint16_t raw = 0;
  const bool ok = readBq27220Reg16Le(BQ27220_CUR_REG, &raw);
  Wire.end();
  pinMode(_i2c.sdaPin, INPUT);
  pinMode(_i2c.sclPin, INPUT);

  _lastCurrentPollMs = now;
  if (!ok) {
    if (_haveBqCurrent) {
      *outMa = _lastGoodCurrentMa;
      return true;
    }
    return false;
  }
  _lastGoodCurrentMa = static_cast<int16_t>(raw);
  _haveBqCurrent = true;
  *outMa = _lastGoodCurrentMa;
  return true;
}

bool BatteryMonitor::isCharging() const {
  if (_mode != Mode::Bq27220) {
    return false;
  }
  // Two attempts (with 2 ms settle between) so a single bus glitch doesn't
  // flip the icon. Mirrors crosspoint-reader/lib/hal/HalGPIO.cpp::isUsbConnected.
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    int16_t ma = 0;
    if (readBq27220Current_(&ma)) {
      return ma > 0;
    }
    delay(2);
  }
  return false;
}

uint16_t BatteryMonitor::percentageFromMillivolts(uint16_t millivolts) {
  double volts = millivolts / 1000.0;
  // Polynomial derived from LiPo samples
  double y = -144.9390 * volts * volts * volts + 1655.8629 * volts * volts - 6158.8520 * volts + 7501.3202;

  // Clamp to [0,100] and round
  y = max(y, 0.0);
  y = min(y, 100.0);
  y = round(y);
  return static_cast<int>(y);
}

uint16_t BatteryMonitor::millivoltsFromRawAdc(uint16_t adc_raw) { return adc_raw; }
