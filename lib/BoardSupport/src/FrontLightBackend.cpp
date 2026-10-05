#include "FrontLightBackend.h"

#include <Arduino.h>
#include <HardwareIdentity.h>
#include <driver/gpio.h>

#include "TargetConfig.h"

namespace papyrix::board {

bool FrontLightBackend::begin() {
  config_ = HardwareIdentity::instance().profile().frontLight;
  if (config_.gpio == kPinUnused) return false;

  gpio_hold_dis(static_cast<gpio_num_t>(config_.gpio));
  if (config_.warmGpio != kPinUnused) gpio_hold_dis(static_cast<gpio_num_t>(config_.warmGpio));
  const bool coolAttached = ledcAttach(config_.gpio, config_.pwmHz, config_.resolutionBits);
  const bool warmAttached =
      config_.warmGpio == kPinUnused || ledcAttach(config_.warmGpio, config_.pwmHz, config_.resolutionBits);
  available_ = coolAttached && warmAttached;
  if (!available_) {
    if (coolAttached) ledcDetach(config_.gpio);
    if (config_.warmGpio != kPinUnused && warmAttached) ledcDetach(config_.warmGpio);
    return false;
  }

  write(0, 50);
  return true;
}

void FrontLightBackend::write(uint8_t brightness, uint8_t warmth) {
  if (!available_) return;
  const auto duty = frontLightDuties(brightness, warmth, config_.resolutionBits, config_.activeHigh);
  ledcWrite(config_.gpio, duty.cool);
  if (config_.warmGpio != kPinUnused) ledcWrite(config_.warmGpio, duty.warm);
}

void FrontLightBackend::end() {
  if (!available_) return;
  write(0, 50);
  ledcDetach(config_.gpio);
  if (config_.warmGpio != kPinUnused) ledcDetach(config_.warmGpio);
  available_ = false;
}

}  // namespace papyrix::board
