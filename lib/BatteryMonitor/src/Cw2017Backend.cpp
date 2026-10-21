#include <Arduino.h>
#include <Cw2017Policy.h>
#include <Wire.h>

#include "BatteryMonitor.h"

namespace {
constexpr uint8_t kVersion = 0x00;
constexpr uint8_t kVcell = 0x02;
constexpr uint8_t kSoc = 0x04;
constexpr uint8_t kMode = 0x08;
constexpr uint8_t kSocAlert = 0x0B;
constexpr uint8_t kBatInfo = 0x10;
constexpr uint8_t kUpdateFlag = 0x80;

constexpr uint8_t kUnhealthyPollLimit = 3;
// Failed recovery attempts use retry intervals from 1 s to 32 s.
constexpr uint8_t kMaxRecoveryBackoffShift = 5;

bool read8(const BatteryMonitor::Cw2017Config& config, uint8_t reg, uint8_t* value) {
  Wire.beginTransmission(config.address);
  Wire.write(reg);
  if (Wire.endTransmission() != 0 || Wire.requestFrom(static_cast<int>(config.address), 1) != 1) return false;
  *value = Wire.read();
  return true;
}

bool write8(const BatteryMonitor::Cw2017Config& config, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(config.address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool waitCw2017Version_(const BatteryMonitor::Cw2017Config& config) {
  for (int attempt = 0; attempt < 50; ++attempt) {
    uint8_t version = 0;
    if (read8(config, kVersion, &version) && papyrix::battery::cw2017VersionIsRunning(version)) return true;
    delay(20);
  }
  return false;
}

}  // namespace

BatteryMonitor::BatteryMonitor(const Cw2017Config& config) : _mode(Mode::Cw2017), _cw2017(config) {}

bool BatteryMonitor::ensureCw2017Profile_() const {
  if (_cw2017Initialized) return true;
  const unsigned long now = millis();
  const uint8_t backoffShift =
      _cw2017RecoveryFailures < kMaxRecoveryBackoffShift ? _cw2017RecoveryFailures : kMaxRecoveryBackoffShift;
  if (_cw2017LastInitAttemptMs && now - _cw2017LastInitAttemptMs < (1000UL << backoffShift)) return false;
  _cw2017LastInitAttemptMs = now;
  if (_cw2017EverInitialized && _cw2017RecoveryFailures < kMaxRecoveryBackoffShift) ++_cw2017RecoveryFailures;
  if (!attemptCw2017Init_()) return false;
  _cw2017EverInitialized = true;
  _cw2017Initialized = true;
  return true;
}

bool BatteryMonitor::attemptCw2017Init_() const {
  if (!Wire.begin(_cw2017.sdaPin, _cw2017.sclPin, _cw2017.freq)) return false;
  Wire.setTimeOut(10);

  uint8_t mode = 0;
  uint8_t version = 0;
  uint8_t alert = 0;
  if (!read8(_cw2017, kMode, &mode) || !read8(_cw2017, kVersion, &version) || !read8(_cw2017, kSocAlert, &alert)) {
    return false;
  }
  if (_cw2017.profile == nullptr) {
    // Classic keeps its factory calibration. It never receives a model write
    // or restart sequence.
    return mode == 0 && (alert & kUpdateFlag) != 0 && papyrix::battery::cw2017VersionIsRunning(version);
  }

  bool profileMatches = (alert & kUpdateFlag) != 0;
  if (profileMatches) {
    for (uint8_t i = 0; i < papyrix::battery::kCw2017BatteryProfile.size(); ++i) {
      uint8_t stored = 0;
      if (!read8(_cw2017, static_cast<uint8_t>(kBatInfo + i), &stored)) return false;
      if (stored != _cw2017.profile[i]) {
        profileMatches = false;
        break;
      }
    }
  }
  if (!profileMatches) {
    for (uint8_t i = 0; i < papyrix::battery::kCw2017BatteryProfile.size(); ++i) {
      if (!write8(_cw2017, static_cast<uint8_t>(kBatInfo + i), _cw2017.profile[i])) return false;
    }
    if (!write8(_cw2017, kSocAlert, kUpdateFlag)) return false;
  }

  if (profileMatches && mode == 0 && papyrix::battery::cw2017VersionIsRunning(version) && !_cw2017RestartPending) {
    return true;
  }
  if (!write8(_cw2017, kMode, 0xF0)) return false;
  delay(20);
  if (!write8(_cw2017, kMode, 0x30)) return false;
  delay(20);
  if (!write8(_cw2017, kMode, 0x00)) return false;
  _cw2017RestartPending = false;
  if (!_cw2017EverInitialized) {
    _cw2017EverInitialized = true;
    _cw2017RecoveryFailures = 1;
  }
  delay(20);
  if (!waitCw2017Version_(_cw2017)) return false;
  return true;
}

bool BatteryMonitor::readCw2017Soc_(uint16_t* out) const {
  const unsigned long now = millis();
  if (!_cw2017SocPolled || now - _lastSocPollMs >= kBqPollIntervalMs) {
    _cw2017SocPolled = true;
    _lastSocPollMs = now;
    uint8_t mode = 0;
    uint8_t version = 0;
    uint8_t soc = 0;
    const bool wasInitialized = _cw2017Initialized;
    if (!_cw2017EverInitialized) ensureCw2017Profile_();
    // Read health before a recovery attempt can restart the gauge.
    if (_cw2017EverInitialized && read8(_cw2017, kMode, &mode) && read8(_cw2017, kVersion, &version) &&
        read8(_cw2017, kSoc, &soc)) {
      const bool registersHealthy = mode == 0 && papyrix::battery::cw2017VersionIsRunning(version);
      if (registersHealthy && soc <= 100) {
        _cw2017UnhealthyPolls = 0;
        _cw2017RecoveryFailures = 0;
        _cw2017RestartPending = false;
        _cw2017Initialized = true;
        _lastGoodSoc = soc;
        _haveCw2017Soc = true;
      } else {
        if (wasInitialized && (!registersHealthy || _cw2017.profile != nullptr) &&
            ++_cw2017UnhealthyPolls >= kUnhealthyPollLimit) {
          _cw2017UnhealthyPolls = 0;
          _cw2017RestartPending = registersHealthy;
          _cw2017Initialized = false;
        }
        if (!_cw2017Initialized && ensureCw2017Profile_()) {
          uint8_t recovered = 0;
          if (read8(_cw2017, kSoc, &recovered) && recovered <= 100) {
            _cw2017RecoveryFailures = 0;
            _lastGoodSoc = recovered;
            _haveCw2017Soc = true;
          }
        }
      }
    }
  }
  if (!_haveCw2017Soc) return false;
  *out = _lastGoodSoc;
  return true;
}

bool BatteryMonitor::readCw2017Mv_(uint16_t* out) const {
  const unsigned long now = millis();
  if (!_cw2017MvPolled || now - _lastMvPollMs >= kBqPollIntervalMs) {
    _cw2017MvPolled = true;
    _lastMvPollMs = now;
    uint8_t high = 0;
    uint8_t low = 0;
    // Only the SOC poll starts recovery after initialization.
    const bool ready = _cw2017EverInitialized ? _cw2017Initialized : ensureCw2017Profile_();
    if (ready && read8(_cw2017, kVcell, &high) && read8(_cw2017, static_cast<uint8_t>(kVcell + 1), &low)) {
      _lastGoodMv = papyrix::battery::cw2017Millivolts(static_cast<uint16_t>(high) << 8 | low);
      _haveCw2017Mv = true;
    }
  }
  if (!_haveCw2017Mv) return false;
  *out = _lastGoodMv;
  return true;
}
