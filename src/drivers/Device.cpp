#include "Device.h"

#include <Arduino.h>
#include <Logging.h>
#include <Preferences.h>
#include <Wire.h>

#define TAG "DEVICE"

namespace papyrix {
namespace drivers {

namespace {

// X3 I2C bus pins (verified against crosspoint-reader/lib/hal/HalGPIO.h).
constexpr int X3_I2C_SDA = 20;
constexpr int X3_I2C_SCL = 0;
constexpr uint32_t X3_I2C_FREQ = 400000;
constexpr uint16_t I2C_TIMEOUT_MS = 6;
constexpr uint16_t INTER_PASS_DELAY_MS = 2;

// I2C device addresses (X3-only chips).
constexpr uint8_t I2C_ADDR_BQ27220 = 0x55;
constexpr uint8_t I2C_ADDR_DS3231 = 0x68;
constexpr uint8_t I2C_ADDR_QMI8658 = 0x6B;
constexpr uint8_t I2C_ADDR_QMI8658_ALT = 0x6A;

// Registers used for sane-value signature checks.
constexpr uint8_t BQ27220_SOC_REG = 0x2C;       // 16-bit LE
constexpr uint8_t BQ27220_VOLT_REG = 0x08;      // 16-bit LE, mV
constexpr uint8_t DS3231_SEC_REG = 0x00;        // 8-bit BCD
constexpr uint8_t QMI8658_WHO_AM_I_REG = 0x00;  // 8-bit
constexpr uint8_t QMI8658_WHO_AM_I_VALUE = 0x05;

// NVS layout. Distinct namespace from crosspoint-reader's "cphw" so a user
// flashing back and forth between firmwares cannot read the wrong cache.
constexpr const char* HW_NAMESPACE = "papyrix_hw";
constexpr const char* NVS_KEY_OVERRIDE = "dev_ovr";
constexpr const char* NVS_KEY_CACHE = "dev_det";

bool readI2CReg8(uint8_t addr, uint8_t reg, uint8_t* out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<int>(addr), 1) != 1) return false;
  if (!Wire.available()) return false;
  *out = Wire.read();
  return true;
}

bool readI2CReg16LE(uint8_t addr, uint8_t reg, uint16_t* out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<int>(addr), 2) != 2) return false;
  if (Wire.available() < 2) return false;
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *out = (static_cast<uint16_t>(hi) << 8) | lo;
  return true;
}

// SOC ∈ [0, 100] AND voltage ∈ [2500, 5000] mV — rules out random ACK matches.
bool probeBQ27220Signature() {
  uint16_t soc = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_SOC_REG, &soc)) return false;
  if (soc > 100) return false;
  uint16_t voltageMv = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_VOLT_REG, &voltageMv)) return false;
  return voltageMv >= 2500 && voltageMv <= 5000;
}

// BCD seconds: tens digit ≤ 5, ones digit ≤ 9. Mask tens to 3 bits because
// the high bit of that nibble is the DS3231 oscillator-halt flag.
bool probeDS3231Signature() {
  uint8_t sec = 0;
  if (!readI2CReg8(I2C_ADDR_DS3231, DS3231_SEC_REG, &sec)) return false;
  const uint8_t tens = (sec >> 4) & 0x07;
  const uint8_t ones = sec & 0x0F;
  return tens <= 5 && ones <= 9;
}

bool probeQMI8658Signature() {
  uint8_t whoami = 0;
  if (readI2CReg8(I2C_ADDR_QMI8658, QMI8658_WHO_AM_I_REG, &whoami) && whoami == QMI8658_WHO_AM_I_VALUE) {
    return true;
  }
  if (readI2CReg8(I2C_ADDR_QMI8658_ALT, QMI8658_WHO_AM_I_REG, &whoami) && whoami == QMI8658_WHO_AM_I_VALUE) {
    return true;
  }
  return false;
}

}  // namespace

Device& Device::instance() {
  static Device inst;
  return inst;
}

const char* Device::cacheDir() const { return type_ == Type::X3 ? "/.papyrix/cache/x3" : "/.papyrix/cache"; }

Device::ProbeReport Device::runProbePass_() {
  ProbeReport r;
  r.bq27220 = probeBQ27220Signature();
  r.ds3231 = probeDS3231Signature();
  r.qmi8658 = probeQMI8658Signature();
  r.score = static_cast<uint8_t>(r.bq27220 + r.ds3231 + r.qmi8658);
  return r;
}

Device::Type Device::runFullProbe_() {
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(I2C_TIMEOUT_MS);

  const ProbeReport pass1 = runProbePass_();
  delay(INTER_PASS_DELAY_MS);
  const ProbeReport pass2 = runProbePass_();

  LOG_INF(TAG, "probe: pass1=%u(bq=%d rtc=%d imu=%d) pass2=%u(bq=%d rtc=%d imu=%d)", pass1.score, pass1.bq27220,
          pass1.ds3231, pass1.qmi8658, pass2.score, pass2.bq27220, pass2.ds3231, pass2.qmi8658);

  // Hand the bus back to whoever owns it next (SD/SPI driver, etc.).
  Wire.end();
  pinMode(X3_I2C_SDA, INPUT);
  pinMode(X3_I2C_SCL, INPUT);

  lastProbe_ = pass2;

  if (pass1.score >= 2 && pass2.score >= 2) return Type::X3;
  if (pass1.score == 0 && pass2.score == 0) return Type::X4;
  return Type::Unknown;
}

Device::Type Device::readOverride_() const {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, true)) return Type::Unknown;
  const uint8_t v = prefs.getUChar(NVS_KEY_OVERRIDE, 0);
  prefs.end();
  if (v == 1) return Type::X4;
  if (v == 2) return Type::X3;
  return Type::Unknown;
}

Device::Type Device::readCache_() const {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, true)) return Type::Unknown;
  const uint8_t v = prefs.getUChar(NVS_KEY_CACHE, 0);
  prefs.end();
  if (v == 1) return Type::X4;
  if (v == 2) return Type::X3;
  return Type::Unknown;
}

void Device::writeCache_(Type t) {
  if (t == Type::Unknown) return;
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, false)) return;
  const uint8_t existing = prefs.getUChar(NVS_KEY_CACHE, 0);
  const uint8_t next = static_cast<uint8_t>(t);
  if (existing != next) {
    prefs.putUChar(NVS_KEY_CACHE, next);
  }
  prefs.end();
}

void Device::probe() {
  const Type ovr = readOverride_();
  if (ovr != Type::Unknown) {
    type_ = ovr;
    LOG_INF(TAG, "override active: %s", isX3() ? "X3" : "X4");
    return;
  }

  const Type cached = readCache_();
  if (cached != Type::Unknown) {
    type_ = cached;
    LOG_INF(TAG, "using cache: %s", isX3() ? "X3" : "X4");
    return;
  }

  LOG_INF(TAG, "no cache, running I2C probe");
  const Type detected = runFullProbe_();
  if (detected == Type::Unknown) {
    type_ = Type::X4;
    LOG_INF(TAG, "probe inconclusive (score=%u), defaulting to X4 (no cache write)", lastProbe_.score);
    return;
  }

  type_ = detected;
  writeCache_(detected);
  LOG_INF(TAG, "probe: %s detected, cached", isX3() ? "X3" : "X4");
}

}  // namespace drivers
}  // namespace papyrix
