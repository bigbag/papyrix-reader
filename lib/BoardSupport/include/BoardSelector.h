#pragma once

#include <DisplayController.h>

#include <cstdint>

#include "BoardProfile.h"

namespace papyrix::board {
class HardwareIdentity;

inline constexpr char kHardwareNamespace[] = "papyrix_hw";
inline constexpr char kBoardOverrideKey[] = "dev_ovr";
inline constexpr char kBoardCacheKey[] = "dev_det";
inline constexpr char kControllerOverrideKey[] = "epd_ovr";
inline constexpr char kControllerCacheKey[] = "epd_det";
inline constexpr char kControllerVersionKey[] = "epd_ver";

inline constexpr uint8_t kStoredAuto = 0;
inline constexpr uint8_t kStoredX4 = 1;
inline constexpr uint8_t kStoredX3 = 2;

struct BoardProbePass {
  bool bq27220 = false;
  bool ds3231 = false;
  bool qmi8658 = false;
  uint8_t score = 0;
};

struct BoardProbeReport {
  BoardProbePass pass1{};
  BoardProbePass pass2{};
};

enum class BoardProbeVerdict : uint8_t { X3, X4, Inconclusive };
enum class BoardSelectionSource : uint8_t { Fixed, Override, Cache, Probe, Fallback };

struct BoardSelection {
  BoardId board = BoardId::X4;
  BoardSelectionSource source = BoardSelectionSource::Fallback;
  bool writeCache = false;
  uint8_t storedValue = kStoredAuto;
  bool invalidOverride = false;
  bool invalidCache = false;
};

BoardProbeVerdict classifyBoardProbe(const BoardProbeReport& report);
BoardSelection resolveBoardSelection(uint8_t overrideValue, uint8_t cacheValue, BoardProbeVerdict liveVerdict);
const char* boardSelectionSourceName(BoardSelectionSource source);

bool selectBoard(HardwareIdentity& identity, bool allowProbe);
void selectPanel(HardwareIdentity& identity);
bool restoreClockPanel(HardwareIdentity& identity, eink::DisplayController panel, uint8_t variant);

}  // namespace papyrix::board
