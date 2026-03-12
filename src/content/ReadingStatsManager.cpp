#include "ReadingStatsManager.h"

#include <Logging.h>
#include <SdFat.h>

#include <ctime>

#include "../core/Core.h"

#define TAG "READ_STATS"

namespace papyrix {

ReadingStatsManager::Snapshot ReadingStatsManager::load(Core& core) {
  Snapshot snapshot;

  FsFile file;
  auto openResult = core.storage.openRead(kPath, file);
  if (!openResult.ok()) {
    resolveCurrentDayKey(snapshot.dayKey);
    snapshot.hasCalendarTime = (snapshot.dayKey >= 0);
    return snapshot;
  }

  PersistedData data;
  const size_t expectedSize = sizeof(PersistedData);
  if (file.size() < static_cast<int64_t>(expectedSize) ||
      file.read(reinterpret_cast<uint8_t*>(&data), expectedSize) != static_cast<int>(expectedSize)) {
    file.close();
    LOG_ERR(TAG, "Failed to read reading stats, using defaults");
    resolveCurrentDayKey(snapshot.dayKey);
    snapshot.hasCalendarTime = (snapshot.dayKey >= 0);
    return snapshot;
  }
  file.close();

  if (data.magic != kMagic || data.version != kVersion) {
    LOG_ERR(TAG, "Invalid reading stats format, resetting");
    resolveCurrentDayKey(snapshot.dayKey);
    snapshot.hasCalendarTime = (snapshot.dayKey >= 0);
    return snapshot;
  }

  snapshot.pagesToday = data.pagesToday;
  snapshot.totalPagesRead = data.totalPagesRead;
  snapshot.dayKey = data.dayKey;

  int32_t currentDayKey = -1;
  snapshot.hasCalendarTime = resolveCurrentDayKey(currentDayKey);
  if (snapshot.hasCalendarTime) {
    if (snapshot.dayKey != currentDayKey) {
      snapshot.dayKey = currentDayKey;
      snapshot.pagesToday = 0;
    }
  }

  return snapshot;
}

bool ReadingStatsManager::recordPages(Core& core, uint32_t pagesRead, Snapshot& snapshot) {
  if (pagesRead == 0) return true;

  int32_t currentDayKey = -1;
  snapshot.hasCalendarTime = resolveCurrentDayKey(currentDayKey);

  if (snapshot.hasCalendarTime) {
    if (snapshot.dayKey != currentDayKey) {
      snapshot.dayKey = currentDayKey;
      snapshot.pagesToday = 0;
    }
  }

  snapshot.pagesToday += pagesRead;
  snapshot.totalPagesRead += pagesRead;

  return save(core, snapshot);
}

bool ReadingStatsManager::resolveCurrentDayKey(int32_t& outDayKey) {
  outDayKey = -1;

  const std::time_t now = std::time(nullptr);
  // Treat clearly invalid timestamps as "clock unavailable".
  static constexpr std::time_t kMinValidEpoch = 1704067200;  // 2024-01-01T00:00:00Z
  if (now < kMinValidEpoch) {
    return false;
  }

  outDayKey = static_cast<int32_t>(now / 86400);
  return true;
}

bool ReadingStatsManager::save(Core& core, const Snapshot& snapshot) {
  PersistedData data;
  data.magic = kMagic;
  data.version = kVersion;
  data.dayKey = snapshot.dayKey;
  data.pagesToday = snapshot.pagesToday;
  data.totalPagesRead = snapshot.totalPagesRead;

  FsFile file;
  auto openResult = core.storage.openWrite(kPath, file);
  if (!openResult.ok()) {
    LOG_ERR(TAG, "Failed to open reading stats for write");
    return false;
  }

  const size_t bytes = sizeof(PersistedData);
  if (file.write(reinterpret_cast<const uint8_t*>(&data), bytes) != static_cast<int>(bytes)) {
    LOG_ERR(TAG, "Failed to persist reading stats");
    file.close();
    return false;
  }

  file.close();
  return true;
}

}  // namespace papyrix
