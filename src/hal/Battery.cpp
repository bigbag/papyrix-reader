#include "Battery.h"

#include <Arduino.h>
#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif
#if PAPYRIX_CAP_BATTERY_ADC
#include <esp_system.h>
#endif
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif

#include <HardwareIdentity.h>
#include <TargetConfig.h>

#include "BatteryChargePolicy.h"
#if PAPYRIX_CAP_BATTERY_ADC
#include "BatteryEstimatePolicy.h"
#endif

#if PAPYRIX_CAP_BATTERY_ADC
namespace {
constexpr uint32_t kAdcPollIntervalMs = 5000;
RTC_NOINIT_ATTR papyrix::hal::battery_estimate::RetainedEstimate retainedX4Estimate;
}  // namespace
#endif

namespace papyrix::hal {

void Battery::init() {
  if (initialized_) return;
  initialized_ = true;

  const auto& config = board::HardwareIdentity::instance().profile().battery;
  if (config.chargeStatusPin != board::kPinUnused) {
    pinMode(config.chargeStatusPin, config.chargeStatusActiveHigh ? INPUT : INPUT_PULLUP);
  }
#if PAPYRIX_CAP_BATTERY_ADC || PAPYRIX_CAP_BATTERY_BQ27220
  if (config.backend == board::BatteryBackend::Adc) {
    monitor_.emplace(static_cast<uint8_t>(config.adcPin));
    adcBackend_ = true;
    if (esp_reset_reason() == ESP_RST_POWERON) retainedX4Estimate = {};
  } else if (config.backend == board::BatteryBackend::Bq27220) {
    monitor_.emplace(BatteryMonitor::Bq27220Config{config.sda, config.scl, config.i2cHz, config.designCapacityMah});
  }
#elif PAPYRIX_CAP_BATTERY_CW2017
  if (config.backend == board::BatteryBackend::Cw2017) {
    monitor_.emplace(
        BatteryMonitor::Cw2017Config{config.sda, config.scl, config.i2cHz, config.i2cAddress, config.gaugeProfile});
  }
#else
  (void)config;
#endif
}

Battery::Status Battery::readStatus() const {
#if PAPYRIX_CAP_BATTERY_ADC || PAPYRIX_CAP_BATTERY_BQ27220 || PAPYRIX_CAP_BATTERY_CW2017
  if (!monitor_) return {};

#if PAPYRIX_CAP_BATTERY_ADC
  const uint32_t now = millis();
  if (adcBackend_ && adcEstimateInitialized_ && now - lastAdcPollMs_ < kAdcPollIntervalMs) {
    return adcCachedStatus_;
  }
#endif

  Status status = monitor_->readStatus();
  const auto& config = board::HardwareIdentity::instance().profile().battery;
  const bool statusHigh = config.chargeStatusPin != board::kPinUnused && digitalRead(config.chargeStatusPin) == HIGH;
  const auto charging = battery_policy::resolveCharging(status.chargingKnown, status.charging, config.chargeStatusPin,
                                                        config.chargeStatusActiveHigh, statusHigh);
  status.chargingKnown = charging.known;
  status.charging = charging.charging;

#if PAPYRIX_CAP_BATTERY_ADC
  if (adcBackend_ && status.percentageKnown) {
    if (!adcEstimateInitialized_) {
      if (!battery_estimate::isUsable(retainedX4Estimate, status.percentage)) {
        retainedX4Estimate = battery_estimate::seed(status.percentage);
      }
      adcEstimateInitialized_ = true;
    } else {
      retainedX4Estimate = battery_estimate::update(retainedX4Estimate, status.percentage);
    }
    status.percentage = battery_estimate::displayPercent(retainedX4Estimate);
    adcCachedStatus_ = status;
    lastAdcPollMs_ = now;
  }
#endif
  return status;
#else
  return {};
#endif
}

bool Battery::serviceDesignCapacity() { return monitor_ && monitor_->serviceDesignCapacity(); }

bool Battery::finishDesignCapacity() { return monitor_ ? monitor_->finishDesignCapacity() : true; }

}  // namespace papyrix::hal
