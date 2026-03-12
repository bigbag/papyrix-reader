#pragma once

#include <cstdint>

namespace papyrix {

struct Core;

// ReadingStatsManager persists lightweight aggregate reading counters.
class ReadingStatsManager {
 public:
  struct Snapshot {
    uint32_t pagesToday = 0;
    uint32_t totalPagesRead = 0;
    int32_t dayKey = -1;  // UTC days since epoch
    bool hasCalendarTime = false;
  };

  // Load persisted counters from disk (or defaults if missing/corrupt).
  static Snapshot load(Core& core);

  // Record newly read pages and persist updated counters.
  static bool recordPages(Core& core, uint32_t pagesRead, Snapshot& snapshot);

 private:
  struct PersistedData {
    uint32_t magic = 0;
    uint8_t version = 0;
    uint8_t reserved0 = 0;
    uint16_t reserved1 = 0;
    int32_t dayKey = -1;
    uint32_t pagesToday = 0;
    uint32_t totalPagesRead = 0;
  };

  static constexpr uint32_t kMagic = 0x53544452;      // "RDTS"
  static constexpr uint8_t kVersion = 1;
  static constexpr const char* kPath = "/.papyrix/reading_stats.bin";

  static bool resolveCurrentDayKey(int32_t& outDayKey);
  static bool save(Core& core, const Snapshot& snapshot);
};

}  // namespace papyrix
