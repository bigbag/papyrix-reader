#pragma once

#include <DisplayController.h>
#include <X3DisplayProbe.h>

#include <cstdint>

namespace papyrix::board {

constexpr uint8_t kStoredControllerAuto = 0;
constexpr uint8_t kStoredUc8253 = 1;
constexpr uint8_t kStoredUc8279X3 = 2;
constexpr uint8_t kControllerCacheVersion = 2;

enum class ControllerSelectionSource : uint8_t { X4Default, Override, Cache, Probe, Fallback };

struct ControllerDecision {
  bool resolved = false;
  papyrix::eink::DisplayController controller = papyrix::eink::DisplayController::UC8253;
  ControllerSelectionSource source = ControllerSelectionSource::Fallback;
  bool writeCache = false;
  uint8_t storedValue = kStoredControllerAuto;
  bool invalidOverride = false;
  bool invalidCache = false;
};

ControllerDecision resolveStoredX3Controller(uint8_t overrideValue, uint8_t cacheValue, uint8_t cacheVersion);
ControllerDecision resolveProbedX3Controller(papyrix::eink::X3DisplayVerdict verdict);
const char* controllerSelectionSourceName(ControllerSelectionSource source);

}  // namespace papyrix::board
