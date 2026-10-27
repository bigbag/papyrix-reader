#include "FrontLight.h"

#include <TargetConfig.h>

#if PAPYRIX_CAP_FRONTLIGHT
#include <Arduino.h>
#include <HardwareIdentity.h>
#include <Preferences.h>
#endif

namespace papyrix::hal {
#if PAPYRIX_CAP_FRONTLIGHT
namespace {
constexpr const char* kNamespace = "frontlight";
}
#endif

void FrontLight::setSmooth(bool smoothTransitions) {
  if (smoothTransitions == smoothTransitions_) return;
  smoothTransitions_ = smoothTransitions;
#if PAPYRIX_CAP_FRONTLIGHT
  // Turning smoothing off snaps a running fade to its target output.
  if (!smoothTransitions_ && backend_.available()) backend_.write(outputBrightness_, outputWarmth_);
#endif
}

void FrontLight::driveOutput(uint8_t outputBrightness, uint8_t outputWarmth) {
#if PAPYRIX_CAP_FRONTLIGHT
  if (!backend_.available()) return;
  const bool wasOff = outputBrightness_ == 0;
  const bool isOff = outputBrightness == 0;
  outputBrightness_ = outputBrightness;
  outputWarmth_ = outputWarmth;
  if (smoothTransitions_ && (backend_.fading() || wasOff != isOff)) {
    backend_.fadeTo(outputBrightness, outputWarmth, kFadeDurationMs);
  } else {
    backend_.write(outputBrightness, outputWarmth);
  }
#else
  outputBrightness_ = outputBrightness;
  outputWarmth_ = outputWarmth;
#endif
}

void FrontLight::init(bool smoothTransitions) {
  if (initialized_) return;
  initialized_ = true;
  smoothTransitions_ = smoothTransitions;
  timedOut_ = false;
#if PAPYRIX_CAP_FRONTLIGHT
  if (!backend_.begin()) return;

  const auto boardId = board::HardwareIdentity::instance().board();
  Preferences preferences;
  if (preferences.begin(kNamespace, true)) {
    brightness_ = preferences.getUChar(board::frontLightBrightnessKey(boardId), 0);
    warmth_ = preferences.getUChar(board::frontLightWarmthKey(boardId), 50);
    preferences.end();
  }
  if (brightness_ > 100) brightness_ = 100;
  if (warmth_ > 100) warmth_ = 100;
  outputBrightness_ = 0;
  outputWarmth_ = warmth_;
  driveOutput(brightness_, warmth_);
#endif
}

void FrontLight::update(uint32_t userIdleMs, uint32_t timeoutMs, bool smoothTransitions) {
  setSmooth(smoothTransitions);
#if PAPYRIX_CAP_FRONTLIGHT
  if (!backend_.available()) return;
  if (timeoutMs == 0) {
    if (timedOut_) {
      timedOut_ = false;
      driveOutput(brightness_, warmth_);
    }
    return;
  }
  if (!timedOut_) {
    if (brightness_ != 0 && userIdleMs >= timeoutMs) {
      timedOut_ = true;
      driveOutput(0, warmth_);
    }
    return;
  }
  if (userIdleMs < timeoutMs) {
    timedOut_ = false;
    driveOutput(brightness_, warmth_);
  }
#else
  (void)userIdleMs;
  (void)timeoutMs;
#endif
}

void FrontLight::shutdown(bool allowFade) {
#if PAPYRIX_CAP_FRONTLIGHT
  if (allowFade && smoothTransitions_ && backend_.available() && (outputBrightness_ != 0 || backend_.fading())) {
    backend_.fadeTo(0, outputWarmth_, kFadeDurationMs);
    outputBrightness_ = 0;
    if (backend_.fading()) delay(kFadeDurationMs);
  }
  backend_.end();
#endif
  outputBrightness_ = 0;
  timedOut_ = false;
  initialized_ = false;
}

bool FrontLight::setBrightness(uint8_t percent) {
#if PAPYRIX_CAP_FRONTLIGHT
  if (!backend_.available()) return false;
  brightness_ = percent > 100 ? 100 : percent;
  if (!timedOut_) driveOutput(brightness_, warmth_);
  return persist(board::frontLightBrightnessKey(board::HardwareIdentity::instance().board()), brightness_);
#else
  (void)percent;
  return false;
#endif
}

bool FrontLight::setWarmth(uint8_t percent) {
#if PAPYRIX_CAP_FRONTLIGHT
  if (!backend_.available()) return false;
  warmth_ = percent > 100 ? 100 : percent;
  if (!timedOut_) driveOutput(brightness_, warmth_);
  return persist(board::frontLightWarmthKey(board::HardwareIdentity::instance().board()), warmth_);
#else
  (void)percent;
  return false;
#endif
}

bool FrontLight::persist(const char* key, uint8_t value) {
#if PAPYRIX_CAP_FRONTLIGHT
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return false;
  const bool stored = preferences.getUChar(key, 0xFF) == value || preferences.putUChar(key, value) == sizeof(value);
  preferences.end();
  return stored;
#else
  (void)key;
  (void)value;
  return false;
#endif
}

}  // namespace papyrix::hal
