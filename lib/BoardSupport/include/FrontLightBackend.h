#pragma once

#include <cstdint>

#include "BoardProfile.h"

namespace papyrix::board {

struct FrontLightDuties {
  uint32_t cool;
  uint32_t warm;
};

constexpr uint32_t frontLightMaxDuty(uint8_t resolutionBits) {
  return resolutionBits >= 31 ? 0x7FFFFFFF : (uint32_t{1} << resolutionBits) - 1;
}

constexpr FrontLightDuties frontLightDuties(uint8_t brightness, uint8_t warmth, uint8_t resolutionBits,
                                            bool activeHigh) {
  const uint32_t full = frontLightMaxDuty(resolutionBits);
  const uint32_t boundedBrightness = brightness > 100 ? 100 : brightness;
  const uint32_t boundedWarmth = warmth > 100 ? 100 : warmth;
  const uint64_t total =
      boundedBrightness == 0 ? 0 : 1 + uint64_t{boundedBrightness} * boundedBrightness * (full - 1) / 10000;
  const uint32_t warm = static_cast<uint32_t>((total * boundedWarmth + 50) / 100);
  const uint32_t cool = static_cast<uint32_t>(total) - warm;
  return activeHigh ? FrontLightDuties{cool, warm} : FrontLightDuties{full - cool, full - warm};
}

constexpr const char* frontLightBrightnessKey(BoardId board) {
  switch (board) {
    case BoardId::X3:
      return "x3_b";
    case BoardId::X4:
      return "x4_b";
    case BoardId::X4Pro:
      return "x4pro_b";
    case BoardId::MurphyM4:
      return "m4_b";
  }
  return "unknown_b";
}

constexpr const char* frontLightWarmthKey(BoardId board) {
  switch (board) {
    case BoardId::X3:
      return "x3_w";
    case BoardId::X4:
      return "x4_w";
    case BoardId::X4Pro:
      return "x4pro_w";
    case BoardId::MurphyM4:
      return "m4_w";
  }
  return "unknown_w";
}

class FrontLightBackend {
 public:
  bool begin();
  void write(uint8_t brightness, uint8_t warmth);
  void end();
  bool available() const { return available_; }

 private:
  FrontLightConfig config_{};
  bool available_ = false;
};

}  // namespace papyrix::board
