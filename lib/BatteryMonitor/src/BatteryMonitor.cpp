#include "BatteryMonitor.h"

#include <AdcMutex.h>
#include <Arduino.h>
#include <Wire.h>
#if PAPYRIX_TARGET_X4PRO || PAPYRIX_TARGET_X4CLASSIC
#include <TargetConfig.h>
#endif

inline float min(const float a, const float b) { return a < b ? a : b; }
inline float max(const float a, const float b) { return a > b ? a : b; }

namespace {
constexpr uint8_t I2C_ADDR_BQ27220 = 0x55;
constexpr uint8_t BQ27220_SOC_REG = 0x2C;   // 16-bit LE, percentage
constexpr uint8_t BQ27220_VOLT_REG = 0x08;  // 16-bit LE, mV
constexpr uint8_t BQ27220_CUR_REG = 0x0C;   // 16-bit LE, signed mA (positive = charging)
constexpr uint8_t BQ27220_TIMEOUT_MS = 6;
// Design Capacity load adapted from the FreeInk SDK BatteryMonitor (MIT; see
// FREEINK_LICENSE). TRM SLUUBD4A 6.1: unseal, full access, CONFIG UPDATE,
// checksummed Data Memory writes, exit with reinit, seal. The X3's gauge
// ignores keys sent back to back, so they go 1.5 s apart.
constexpr uint8_t BQ27220_CONTROL = 0x00;  // subcommands
constexpr uint8_t BQ27220_FULL_CHARGE_CAPACITY = 0x12;
constexpr uint8_t BQ27220_OPERATION_STATUS = 0x3A;  // CFGUPDATE bit 10, SEC[1:0] bits 2:1
constexpr uint8_t BQ27220_DESIGN_CAPACITY = 0x3C;
constexpr uint8_t BQ27220_MAC_CONTROL = 0x3E;
constexpr uint8_t BQ27220_MAC_DATA = 0x40;
constexpr uint8_t BQ27220_MAC_DATA_SUM = 0x60;  // MACDataLen() in the high byte
constexpr uint16_t BQ27220_DM_LEARNED_FCC = 0x929D;
constexpr uint16_t BQ27220_DM_DESIGN_CAPACITY = 0x929F;
// Unseal, full access, ENTER_CFG_UPDATE.
constexpr uint16_t BQ27220_UNLOCK[] = {0x0414, 0x3672, 0xFFFF, 0xFFFF, 0x0090};

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
bool writeBq27220Reg16Le(uint8_t reg, uint16_t val) {
  Wire.beginTransmission(I2C_ADDR_BQ27220);
  Wire.write(reg);
  // One I2C write per word: the X3's gauge ignores a subcommand split in two.
  Wire.write(static_cast<uint8_t>(val & 0xFF));
  Wire.write(static_cast<uint8_t>(val >> 8));
  return Wire.endTransmission(true) == 0;
}

// A learning cycle moves Learned Full Charge Capacity at most 256 mAh down
// (TRM 1.1.3), so one learned against TI's 3000 mAh default stays far above a
// small cell.
bool bq27220TooHigh(uint16_t learned, uint16_t mah) { return learned > mah + mah / 4; }

template <typename Op>
bool bqSession(const BatteryMonitor::Bq27220Config& i2c, Op&& op) {
  Wire.begin(i2c.sdaPin, i2c.sclPin, i2c.freq);
  Wire.setTimeOut(BQ27220_TIMEOUT_MS);
  const bool ok = op();
  Wire.end();
  // Hand the pins back so the shared SDA line stays available between calls.
  pinMode(i2c.sdaPin, INPUT);
  pinMode(i2c.sclPin, INPUT);
  return ok;
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
    // Use a safe pre-first-success value so a transient I2C error cannot
    // trigger the low-battery UI before the first valid gauge sample.
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
#if PAPYRIX_CAP_BATTERY_CW2017
  uint16_t percentage = 0;
  return readCw2017Soc_(&percentage) ? percentage : 0;
#else
  if (_mode == Mode::Bq27220) return readBq27220Soc_();
  return percentageFromMillivolts(readMillivolts());
#endif
}

uint16_t BatteryMonitor::readMillivolts() const {
#if PAPYRIX_CAP_BATTERY_CW2017
  uint16_t millivolts = 0;
  return readCw2017Mv_(&millivolts) ? millivolts : 0;
#else
  if (_mode == Mode::Bq27220) return readBq27220Mv_();
  return static_cast<uint16_t>(readRawMillivolts() * _dividerMultiplier);
#endif
}

uint16_t BatteryMonitor::readRawMillivolts() const {
#if PAPYRIX_CAP_BATTERY_CW2017
  return readMillivolts();
#else
  if (_mode == Mode::Bq27220) return readBq27220Mv_();
  std::lock_guard<std::mutex> adcLock(papyrix::board::adcMutex);
  return analogReadMilliVolts(_adcPin);
#endif
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

BatteryMonitor::Status BatteryMonitor::readStatus() const {
  Status status;
#if !PAPYRIX_CAP_BATTERY_CW2017
  if (_mode == Mode::Adc) {
    status.supported = true;
    status.millivolts = readMillivolts();
    status.millivoltsKnown = true;
    status.percentage = percentageFromMillivolts(status.millivolts);
    status.percentageKnown = true;
    return status;
  }
#endif
  status.supported = true;
#if PAPYRIX_CAP_BATTERY_CW2017
  status.percentageKnown = readCw2017Soc_(&status.percentage);
  status.millivoltsKnown = readCw2017Mv_(&status.millivolts);
#else
  status.percentage = readPercentage();
  status.millivolts = readMillivolts();
  status.percentageKnown = true;
  status.millivoltsKnown = true;
  if (_mode == Mode::Bq27220) {
    int16_t current = 0;
    status.chargingKnown = readBq27220Current_(&current);
    status.charging = status.chargingKnown && current > 0;
  }
#endif
  return status;
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

bool BatteryMonitor::serviceDesignCapacity() {
  if (_mode != Mode::Bq27220 || _i2c.designCapacityMah == 0) return false;
  return bq27220LoadStep_(millis());
}

bool BatteryMonitor::finishDesignCapacity() {
  if (_mode != Mode::Bq27220 || _i2c.designCapacityMah == 0) return true;
  Bq27220Load& s = _bqLoad;
  // Decide from the gauge, not from the step flags: the service machine clears
  // its cfg flag as soon as it sends the exit, while the gauge may still be in
  // CONFIG UPDATE for seconds.
  uint16_t status = 0;
  if (!bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_OPERATION_STATUS, &status); })) return false;
  if (s.step == 9 && !(status & 0x0400) && ((status >> 1) & 3) == 3) return true;
  // The machine may have sent ENTER_CFG_UPDATE right before sleep; the gauge
  // sets the bit asynchronously, so a single read can miss it. Wait for the
  // entry to land before exiting, bounded at 2 s.
  if (s.cfg && !(status & 0x0400)) {
    for (int i = 0; i < 20; ++i) {
      delay(100);
      if (bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_OPERATION_STATUS, &status); }) && (status & 0x0400)) {
        break;
      }
    }
  }
  bool ok = true;
  if (status & 0x0400) {
    ok = bqSession(_i2c, [&] { return writeBq27220Reg16Le(BQ27220_CONTROL, s.wrote ? 0x0091 : 0x0092); });
    // The exit runs a reinit; CFGUPDATE clears only when the gauge has really
    // left CONFIG UPDATE. Bound the wait at 5 s.
    bool cleared = false;
    for (int i = 0; i < 50; ++i) {
      delay(100);
      uint16_t poll = 0;
      if (bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_OPERATION_STATUS, &poll); }) && !(poll & 0x0400)) {
        cleared = true;
        break;
      }
    }
    ok = ok && cleared;
  }
  if (ok) {
    // Seal and verify: SEC[1:0] must read 3 and CONFIG UPDATE must be left.
    ok = bqSession(_i2c, [&] { return writeBq27220Reg16Le(BQ27220_CONTROL, 0x0030); });
    uint16_t sealedStatus = 0;
    ok = ok && bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_OPERATION_STATUS, &sealedStatus); }) &&
         ((sealedStatus >> 1) & 3) == 3 && !(sealedStatus & 0x0400);
  }
  // Record done only for a verified closure; a failed close-out stays pending
  // so the next call retries from the actual gauge state.
  if (ok) s.step = 9;
  return ok;
}

// Replaces TI's default Design Capacity, or a Learned Full Charge Capacity that
// is too high, in one big-endian word, adjusting the block checksum by the
// bytes that change.
bool BatteryMonitor::bq27220WriteParam_(const uint16_t address, const uint16_t mah) {
  uint16_t old = 0;
  uint16_t sum = 0;
  if (!bqSession(_i2c, [&] { return writeBq27220Reg16Le(BQ27220_MAC_CONTROL, address); })) return false;
  delay(10);
  if (!bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_MAC_DATA, &old); }) ||
      !bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_MAC_DATA_SUM, &sum); }))
    return false;
  const uint16_t value = __builtin_bswap16(old);
  if (address == BQ27220_DM_LEARNED_FCC ? !bq27220TooHigh(value, mah) : value != 3000) return true;
  _bqLoad.wrote = true;
  sum = (sum & 0xFF00) | static_cast<uint8_t>(sum + (old & 0xFF) + (old >> 8) - (mah & 0xFF) - (mah >> 8));
  // Reading MACDataSum() moves the X3's gauge to the next block: select this
  // one again.
  if (!bqSession(_i2c, [&] { return writeBq27220Reg16Le(BQ27220_MAC_CONTROL, address); })) return false;
  delay(10);
  return bqSession(_i2c, [&] { return writeBq27220Reg16Le(BQ27220_MAC_DATA, __builtin_bswap16(mah)); }) &&
         bqSession(_i2c, [&] { return writeBq27220Reg16Le(BQ27220_MAC_DATA_SUM, sum); });
}

bool BatteryMonitor::bq27220LoadStep_(const unsigned long now) {
  const uint16_t mah = _i2c.designCapacityMah;
  Bq27220Load& s = _bqLoad;
  if (s.step == 9 || static_cast<long>(now - s.at) < 0) return s.step != 9;
  const auto next = [&](const uint8_t step, const unsigned long gap) {
    s.step = step;
    s.at = millis() + gap;  // schedule from completion, not loop entry
    return true;
  };
  uint16_t status = 0;
  // Leaves CONFIG UPDATE (with reinit once a block was written), then seals.
  const auto close = [&] {
    if (s.cfg) {
      if (bqSession(_i2c, [&] { return writeBq27220Reg16Le(BQ27220_CONTROL, s.wrote ? 0x0091 : 0x0092); })) {
        s.cfg = false;
        s.exitSent = true;
        s.since = millis();
        return next(8, 500);
      }
      // A failed exit must never declare done: the gauge would sit in CONFIG
      // UPDATE with nothing left to service it. Stay pending and retry with a
      // growing gap; step 8 re-enters close on each attempt.
      if (s.exitRetries < 8) ++s.exitRetries;
      s.since = millis();
      return next(8, 500u << (s.exitRetries > 4 ? 4 : s.exitRetries));
    }
    if (s.exitSent) {
      // The exit was accepted but no successful read has observed CFGUPDATE
      // clear yet. Done needs that observation, so keep waiting with backoff.
      if (s.exitRetries < 8) ++s.exitRetries;
      s.since = millis();
      return next(8, 500u << (s.exitRetries > 4 ? 4 : s.exitRetries));
    }
    if (!bqSession(_i2c, [&] { return writeBq27220Reg16Le(BQ27220_CONTROL, 0x0030); }) ||
        !bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_OPERATION_STATUS, &status); }) ||
        ((status >> 1) & 3) != 3 || (status & 0x0400) != 0) {
      // The seal write or its verification failed: stay pending, step 8
      // re-enters close and retries.
      if (s.exitRetries < 8) ++s.exitRetries;
      s.since = millis();
      return next(8, 500u << (s.exitRetries > 4 ? 4 : s.exitRetries));
    }
    s.step = 9;
    return false;
  };
  if (s.step == 0) {
    uint16_t dc = 0;
    uint16_t fcc = 0;
    s.step = 9;
    if (!bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_DESIGN_CAPACITY, &dc); }) ||
        !bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_OPERATION_STATUS, &status); }))
      return false;
    s.cfg = status & 0x0400;
    const uint8_t security = (status >> 1) & 3;
    if (dc == mah && (s.cfg || security != 3)) {
      s.wrote = true;  // an earlier load was cut short
      return close();
    }
    if (dc == mah ? !bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_FULL_CHARGE_CAPACITY, &fcc); }) ||
                        !bq27220TooHigh(fcc, mah)
                  : dc != 3000)
      return false;
    s.step = security == 3 ? 1 : security == 1 ? 5 : 3;
    s.since = now;  // no key was sent in this sequence yet
  }
  if (s.step <= 5) {
    // The gauge drops a second key sent more than 4 s after the first. Blocking
    // loop work can outlive that window, so start the pair over once; a second
    // lost window gives up for this boot.
    if ((s.step == 2 || s.step == 4) && now - s.since >= 4000) {
      if (++s.restarts > 1) return close();
      s.step = 1;
    }
    s.cfg |= s.step == 5;
    if (!bqSession(_i2c, [&] { return writeBq27220Reg16Le(BQ27220_CONTROL, BQ27220_UNLOCK[s.step - 1]); }))
      return close();
    s.since = millis();  // the gauge times the pair from key receipt
    return next(s.step + 1, s.step == 5 ? 500 : 1500);
  }
  if (s.step == 6 || s.step == 8) {
    const bool reached = bqSession(_i2c, [&] { return readBq27220Reg16Le(BQ27220_OPERATION_STATUS, &status); }) &&
                         ((status & 0x0400) != 0) == (s.step == 6);
    if (!reached && now - s.since < 5000) return next(s.step, 500);
    // Learned FCC first, Design Capacity half a second later: the X3's gauge
    // can miss a block select sent right after a block write. If anything
    // fails, Design Capacity still reads 3000 or FullChargeCapacity() too
    // high, and the next start retries.
    if (s.step == 6 && reached && bq27220WriteParam_(BQ27220_DM_LEARNED_FCC, mah)) return next(7, 500);
    if (s.step == 8 && reached) s.exitSent = false;  // clear observed on a good read
  } else {
    bq27220WriteParam_(BQ27220_DM_DESIGN_CAPACITY, mah);
  }
  return close();
}
