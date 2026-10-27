#pragma once

#include <FrontLightBackend.h>

#include <cstdint>

namespace papyrix::hal {

class FrontLight {
 public:
  static constexpr uint32_t kFadeDurationMs = 500;

  void init(bool smoothTransitions);
  void update(uint32_t userIdleMs, uint32_t timeoutMs, bool smoothTransitions);
  void shutdown(bool allowFade);

  bool isAvailable() const { return backend_.available(); }
  uint8_t brightness() const { return brightness_; }
  uint8_t warmth() const { return warmth_; }
  bool setBrightness(uint8_t percent);
  bool setWarmth(uint8_t percent);

 private:
  bool persist(const char* key, uint8_t value);
  void driveOutput(uint8_t outputBrightness, uint8_t outputWarmth);
  void setSmooth(bool smoothTransitions);

  board::FrontLightBackend backend_;
  uint8_t brightness_ = 0;
  uint8_t warmth_ = 50;
  uint8_t outputBrightness_ = 0;
  uint8_t outputWarmth_ = 50;
  bool initialized_ = false;
  bool smoothTransitions_ = false;
  bool timedOut_ = false;
};

}  // namespace papyrix::hal
