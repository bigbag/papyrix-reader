#pragma once

#include <cstddef>
#include <cstdint>

#include "BoardProfile.h"

namespace papyrix::board {

struct TouchPosition {
  int16_t x = 0;
  int16_t y = 0;
};

struct RawTouchSample {
  bool fresh = false;
  bool controllerOk = false;
  // Successful polls retain the contact state when fresh is false.
  uint8_t contactCount = 0;
  uint16_t rawX = 0;
  uint16_t rawY = 0;
  bool homeDown = false;
};

void enableTouch(const TouchConfig& touch);
bool decodeGt911Frame(const TouchConfig& config, uint8_t status, const uint8_t* pointData, size_t pointLength,
                      RawTouchSample& sample);
void suppressTouchUntilIdle();
uint32_t touchSuppressionRevision();

struct TouchSample {
  bool fresh = false;
  bool controllerOk = false;
  uint8_t contactCount = 0;
  TouchPosition position{};
};

struct TapFrame {
  bool down = false;
  bool up = false;
  bool tap = false;
  TouchPosition position{};
};

class TapClassifier {
 public:
  static constexpr int16_t MOVEMENT_LIMIT = 24;
  static constexpr uint32_t DEBOUNCE_MS = 40;
  static constexpr uint32_t STALE_CONTACT_MS = 100;

  TapFrame update(const TouchSample& sample, uint32_t nowMs, bool enabled, bool refreshActive,
                  uint8_t orientationRevision);
  void resetAfterWake();

 private:
  TapFrame cancelActive(uint32_t nowMs, bool ignoreUntilIdle);

  bool active_ = false;
  bool moved_ = false;
  bool ignoreUntilIdle_ = false;
  bool haveOrientation_ = false;
  uint8_t orientationRevision_ = 0;
  uint32_t lastGoodAtMs_ = 0;
  uint32_t lastReleaseAtMs_ = 0;
  TouchPosition downPosition_{};
  TouchPosition lastPosition_{};
};

class TouchBackend {
 public:
  bool begin(const BoardProfile& profile);
  RawTouchSample poll();
  void shutdown();
  bool available() const { return available_; }
  const TouchConfig& config() const { return config_; }

 private:
  bool beginGt911();
  RawTouchSample pollGt911();
  bool readGt911(uint16_t reg, uint8_t* data, uint8_t length);
  bool clearGt911Status();

  TouchConfig config_{};
  uint8_t address_ = 0;
  bool available_ = false;
  uint8_t contactCount_ = 0;
  bool homeDown_ = false;
};

}  // namespace papyrix::board
