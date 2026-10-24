#include "TouchBackend.h"

#include <atomic>

#if PAPYRIX_TARGET_X4PRO || PAPYRIX_TARGET_STICKY || PAPYRIX_TARGET_MURPHY_M4
#include <TargetConfig.h>
#endif
#if PAPYRIX_CAP_TOUCH
#include <Arduino.h>
#if !PAPYRIX_TARGET_MURPHY_M4
#include <Wire.h>
#endif
#include <driver/gpio.h>
#endif
#if PAPYRIX_TARGET_MURPHY_M4
#include "Ft6336uBackend.h"
#endif

namespace papyrix::board {
namespace {

int16_t absolute(int16_t value) { return value < 0 ? static_cast<int16_t>(-value) : value; }
std::atomic<uint32_t> gTouchSuppressionRevision{0};
std::atomic<uint32_t> gTouchInvalidationRevision{0};

}  // namespace

void suppressTouchUntilIdle() {
  ++gTouchSuppressionRevision;
  if (gTouchSuppressionRevision == 0) ++gTouchSuppressionRevision;
}

uint32_t touchSuppressionRevision() { return gTouchSuppressionRevision; }

void discardPendingTouch() {
  ++gTouchInvalidationRevision;
  suppressTouchUntilIdle();
}

uint32_t touchInvalidationRevision() { return gTouchInvalidationRevision; }

bool pauseTouchForDisplayReset() {
#if PAPYRIX_TARGET_MURPHY_M4
  return ft6336uBackend().pauseForDisplayReset();
#else
  return true;
#endif
}

bool resumeTouchAfterDisplayReset() {
#if PAPYRIX_TARGET_MURPHY_M4
  return ft6336uBackend().resumeAfterDisplayReset();
#else
  return true;
#endif
}

bool stopTouchAcquisition() {
#if PAPYRIX_TARGET_MURPHY_M4
  return ft6336uBackend().shutdown();
#else
  return true;
#endif
}

void enableTouch(const TouchConfig& touch) {
#if PAPYRIX_CAP_TOUCH
  if (touch.powerPin != kPinUnused) {
    gpio_hold_dis(static_cast<gpio_num_t>(touch.powerPin));
    pinMode(touch.powerPin, OUTPUT);
    digitalWrite(touch.powerPin, touch.powerActiveHigh ? HIGH : LOW);
  }
#if PAPYRIX_TARGET_MURPHY_M4
  delay(500);
  return;
#endif
  delay(50);
  if (touch.irq == kPinUnused || touch.rst == kPinUnused) return;
  gpio_hold_dis(static_cast<gpio_num_t>(touch.rst));
  gpio_hold_dis(static_cast<gpio_num_t>(touch.irq));
  pinMode(touch.irq, OUTPUT);
  pinMode(touch.rst, OUTPUT);
  digitalWrite(touch.rst, LOW);
  digitalWrite(touch.irq, LOW);
  delay(10);
  digitalWrite(touch.rst, HIGH);
  delay(10);
  digitalWrite(touch.irq, LOW);
  delay(50);
  pinMode(touch.irq, INPUT);
  delay(50);
#else
  (void)touch;
#endif
}

bool decodeGt911Frame(const TouchConfig& config, uint8_t status, const uint8_t* pointData, size_t pointLength,
                      RawTouchSample& sample) {
  sample = {};
  sample.controllerOk = true;
  if ((status & 0x80) == 0) return true;
  sample.fresh = true;
  sample.homeDown = (status & 0x10) != 0;
  sample.contactCount = static_cast<uint8_t>(status & 0x0F);
  if (sample.contactCount != 1) return true;
  const size_t offset = config.coordinatesAtByte0 ? 0 : 1;
  if (!pointData || pointLength < offset + 4) return false;
  sample.rawX = static_cast<uint16_t>(pointData[offset]) | (static_cast<uint16_t>(pointData[offset + 1]) << 8);
  sample.rawY = static_cast<uint16_t>(pointData[offset + 2]) | (static_cast<uint16_t>(pointData[offset + 3]) << 8);
  return true;
}

TapFrame TapClassifier::cancelActive(uint32_t nowMs, bool ignoreUntilIdle) {
  TapFrame frame;
  if (active_) {
    frame.up = true;
    frame.position = lastPosition_;
  }
  active_ = false;
  moved_ = true;
  ignoreUntilIdle_ = ignoreUntilIdle;
  lastReleaseAtMs_ = nowMs;
  return frame;
}

TapFrame TapClassifier::update(const TouchSample& sample, uint32_t nowMs, bool enabled, bool refreshActive,
                               uint8_t orientationRevision) {
  if (!enabled || refreshActive) {
    TapFrame frame = cancelActive(nowMs, true);
    if (sample.fresh && sample.contactCount == 0) ignoreUntilIdle_ = false;
    return frame;
  }

  if (active_ && haveOrientation_ && orientationRevision != orientationRevision_) {
    orientationRevision_ = orientationRevision;
    return cancelActive(nowMs, true);
  }
  orientationRevision_ = orientationRevision;
  haveOrientation_ = true;

  if (!sample.fresh) {
    if (sample.controllerOk && sample.contactCount == 0 && !active_) ignoreUntilIdle_ = false;
    if (active_ && !sample.controllerOk && nowMs - lastGoodAtMs_ >= STALE_CONTACT_MS) {
      return cancelActive(nowMs, true);
    }
    return {};
  }

  if (sample.contactCount == 0) {
    if (!active_) {
      ignoreUntilIdle_ = false;
      moved_ = false;
      return {};
    }
    TapFrame frame;
    frame.up = true;
    frame.tap = !moved_;
    frame.position = frame.tap ? downPosition_ : lastPosition_;
    active_ = false;
    lastReleaseAtMs_ = nowMs;
    moved_ = false;
    return frame;
  }

  if (sample.contactCount != 1) return cancelActive(nowMs, true);
  if (ignoreUntilIdle_) return {};

  if (!active_) {
    if (lastReleaseAtMs_ != 0 && nowMs - lastReleaseAtMs_ < DEBOUNCE_MS) {
      ignoreUntilIdle_ = true;
      return {};
    }
    active_ = true;
    moved_ = false;
    lastGoodAtMs_ = nowMs;
    downPosition_ = sample.position;
    lastPosition_ = sample.position;
    TapFrame frame;
    frame.down = true;
    frame.position = sample.position;
    return frame;
  }

  lastGoodAtMs_ = nowMs;
  lastPosition_ = sample.position;
  if (absolute(static_cast<int16_t>(lastPosition_.x - downPosition_.x)) > MOVEMENT_LIMIT ||
      absolute(static_cast<int16_t>(lastPosition_.y - downPosition_.y)) > MOVEMENT_LIMIT) {
    moved_ = true;
  }
  return {};
}

void TapClassifier::resetAfterWake() {
  active_ = false;
  moved_ = false;
  ignoreUntilIdle_ = true;
  haveOrientation_ = false;
  lastGoodAtMs_ = 0;
  lastReleaseAtMs_ = 0;
  downPosition_ = {};
  lastPosition_ = {};
}

bool TouchBackend::begin(const BoardProfile& profile) {
  config_ = profile.touch;
  address_ = 0;
  available_ = false;
#if PAPYRIX_CAP_TOUCH
  contactCount_ = 0;
  homeDown_ = false;
#if PAPYRIX_TARGET_MURPHY_M4
  if (config_.controller == TouchController::Ft6336u) available_ = ft6336uBackend().begin(profile);
#else
  if (config_.controller == TouchController::Gt911) {
    enableTouch(config_);
    available_ = beginGt911();
    if (!available_) shutdown();
  }
#endif
#endif
  return available_;
}

bool TouchBackend::available() const {
#if PAPYRIX_TARGET_MURPHY_M4
  if (config_.controller == TouchController::Ft6336u) return ft6336uBackend().available();
#endif
  return available_;
}

RawTouchSample TouchBackend::poll() {
  if (!available()) return {};
#if PAPYRIX_TARGET_MURPHY_M4
  return ft6336uBackend().poll();
#elif PAPYRIX_CAP_TOUCH
  return pollGt911();
#else
  return {};
#endif
}

void TouchBackend::shutdown() {
#if PAPYRIX_CAP_TOUCH
  if (config_.controller == TouchController::None) return;
#if PAPYRIX_TARGET_MURPHY_M4
  if (!stopTouchAcquisition()) return;
#else
  if (config_.rst != kPinUnused) {
    pinMode(config_.rst, OUTPUT);
    digitalWrite(config_.rst, LOW);
  }
#endif
  if (config_.powerPin != kPinUnused) {
    pinMode(config_.powerPin, OUTPUT);
    digitalWrite(config_.powerPin, config_.powerActiveHigh ? LOW : HIGH);
  }
#endif
  address_ = 0;
  available_ = false;
  config_.controller = TouchController::None;
}

bool TouchBackend::beginGt911() {
#if PAPYRIX_CAP_TOUCH && !PAPYRIX_TARGET_MURPHY_M4
  const auto probe = [this]() {
    for (const uint8_t candidate : {config_.i2cAddress, config_.i2cAddressAlt}) {
      if (candidate == 0) continue;
      Wire.beginTransmission(candidate);
      if (Wire.endTransmission() == 0) {
        address_ = candidate;
        return true;
      }
    }
    return false;
  };
  const auto reset = [this](uint8_t interruptLevel) {
    if (config_.irq == kPinUnused || config_.rst == kPinUnused) return;
    pinMode(config_.irq, OUTPUT);
    pinMode(config_.rst, OUTPUT);
    digitalWrite(config_.rst, LOW);
    digitalWrite(config_.irq, interruptLevel);
    delay(10);
    digitalWrite(config_.rst, HIGH);
    delay(10);
    digitalWrite(config_.irq, interruptLevel);
    delay(50);
    pinMode(config_.irq, INPUT);
    delay(50);
  };

  Wire.begin(config_.sda, config_.scl, config_.i2cHz);
  Wire.setTimeOut(10);
  if (probe()) return true;
  reset(HIGH);
  return probe();
#else
  return false;
#endif
}

RawTouchSample TouchBackend::pollGt911() {
#if PAPYRIX_CAP_TOUCH && !PAPYRIX_TARGET_MURPHY_M4
  uint8_t status = 0;
  if (!readGt911(0x814E, &status, 1)) return {false, false, 0, 0, 0};
  uint8_t point[8]{};
  if ((status & 0x80) != 0 && (status & 0x0F) == 1 && !readGt911(0x8150, point, sizeof(point))) {
    return {false, false, 0, 0, 0};
  }
  RawTouchSample sample{};
  if (!decodeGt911Frame(config_, status, point, sizeof(point), sample)) return {false, false, 0, 0, 0};
  if (sample.fresh && !clearGt911Status()) return {false, false, 0, 0, 0};
  if (sample.fresh) {
    contactCount_ = sample.contactCount;
    homeDown_ = sample.homeDown;
  } else {
    sample.contactCount = contactCount_;
    sample.homeDown = homeDown_;
  }
  return sample;
#else
  return {};
#endif
}

bool TouchBackend::readGt911(uint16_t reg, uint8_t* data, uint8_t length) {
#if PAPYRIX_CAP_TOUCH && !PAPYRIX_TARGET_MURPHY_M4
  Wire.beginTransmission(address_);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address_, length, static_cast<uint8_t>(true)) != length) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
#else
  (void)reg;
  (void)data;
  (void)length;
  return false;
#endif
}

bool TouchBackend::clearGt911Status() {
#if PAPYRIX_CAP_TOUCH && !PAPYRIX_TARGET_MURPHY_M4
  Wire.beginTransmission(address_);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(static_cast<uint8_t>(0));
  return Wire.endTransmission() == 0;
#else
  return false;
#endif
}

}  // namespace papyrix::board
