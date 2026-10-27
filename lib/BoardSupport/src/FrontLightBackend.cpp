#include "FrontLightBackend.h"

#include <Arduino.h>
#include <HardwareIdentity.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

#include "TargetConfig.h"

namespace papyrix::board {

FrontLightDuties FrontLightBackend::dutiesFor(uint8_t brightness, uint8_t warmth) const {
  return frontLightDuties(brightness, warmth, config_.resolutionBits, config_.activeHigh);
}

bool FrontLightBackend::fadeActive() {
  if (!fading_) return false;
  if (static_cast<uint32_t>(millis()) - fadeStartMs_ >= fadeDurationMs_) {
    fading_ = false;
    return false;
  }
  return true;
}

bool FrontLightBackend::fading() const {
  return fading_ && static_cast<uint32_t>(millis()) - fadeStartMs_ < fadeDurationMs_;
}

FrontLightDuties FrontLightBackend::currentDuties() {
  if (!fadeActive()) return outputDuties_;
  // Retarget from the duty the hardware outputs now, not the old target.
  FrontLightDuties current = outputDuties_;
  current.cool = ledcRead(static_cast<uint8_t>(config_.gpio));
  if (config_.warmGpio != kPinUnused) current.warm = ledcRead(static_cast<uint8_t>(config_.warmGpio));
  return current;
}

void FrontLightBackend::stopFades() {
#if PAPYRIX_CAP_FRONTLIGHT
  if (coolFadeStarted_) ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  if (warmFadeStarted_) ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
#endif
  coolFadeStarted_ = false;
  warmFadeStarted_ = false;
}

void FrontLightBackend::writeDutiesImmediate(const FrontLightDuties& duties) {
  stopFades();
  ledcWrite(static_cast<uint8_t>(config_.gpio), duties.cool);
  if (config_.warmGpio != kPinUnused) ledcWrite(static_cast<uint8_t>(config_.warmGpio), duties.warm);
  fading_ = false;
  outputDuties_ = duties;
}

bool FrontLightBackend::begin() {
  config_ = HardwareIdentity::instance().profile().frontLight;
  if (config_.gpio == kPinUnused) return false;

  gpio_hold_dis(static_cast<gpio_num_t>(config_.gpio));
  if (config_.warmGpio != kPinUnused) gpio_hold_dis(static_cast<gpio_num_t>(config_.warmGpio));
  // Fixed channels let shutdown stop fades before Arduino frees their handles.
  const bool coolAttached = ledcAttachChannel(config_.gpio, config_.pwmHz, config_.resolutionBits, LEDC_CHANNEL_0);
  const bool warmAttached = config_.warmGpio == kPinUnused ||
                            ledcAttachChannel(config_.warmGpio, config_.pwmHz, config_.resolutionBits, LEDC_CHANNEL_1);
  available_ = coolAttached && warmAttached;
  if (!available_) {
    if (coolAttached) ledcDetach(config_.gpio);
    if (config_.warmGpio != kPinUnused && warmAttached) ledcDetach(config_.warmGpio);
    return false;
  }

  fading_ = false;
  write(0, 50);
  return true;
}

void FrontLightBackend::write(uint8_t brightness, uint8_t warmth) {
  if (!available_) return;
  writeDutiesImmediate(dutiesFor(brightness, warmth));
}

bool FrontLightBackend::fadeTo(uint8_t brightness, uint8_t warmth, uint32_t durationMs) {
  if (!available_) return false;
  const FrontLightDuties target = dutiesFor(brightness, warmth);
  if (durationMs == 0) {
    writeDutiesImmediate(target);
    return true;
  }
  const FrontLightDuties current = currentDuties();
  if (current.cool == target.cool && current.warm == target.warm) {
    writeDutiesImmediate(target);
    return true;
  }
  stopFades();
  fadeStartMs_ = static_cast<uint32_t>(millis());
  fadeDurationMs_ = durationMs;
  fading_ = true;
  outputDuties_ = target;

  coolFadeStarted_ =
      ledcFade(static_cast<uint8_t>(config_.gpio), current.cool, target.cool, static_cast<int>(durationMs));
  warmFadeStarted_ = config_.warmGpio != kPinUnused && ledcFade(static_cast<uint8_t>(config_.warmGpio), current.warm,
                                                                target.warm, static_cast<int>(durationMs));
  if (coolFadeStarted_ && (config_.warmGpio == kPinUnused || warmFadeStarted_)) return true;

  writeDutiesImmediate(target);
  return false;
}

void FrontLightBackend::end() {
  if (!available_) return;
  write(0, 50);
  ledcDetach(config_.gpio);
  if (config_.warmGpio != kPinUnused) ledcDetach(config_.warmGpio);
  available_ = false;
  fading_ = false;
}

}  // namespace papyrix::board
