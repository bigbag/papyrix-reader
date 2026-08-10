#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace papyrix {

struct ReadingStatsRecord {
  std::string path;
  uint32_t totalSeconds = 0;
  uint32_t sessionCount = 0;
  uint8_t progressPercent = 0;
  bool hasProgress = false;

  bool operator==(const ReadingStatsRecord& other) const {
    return path == other.path && totalSeconds == other.totalSeconds && sessionCount == other.sessionCount &&
           progressPercent == other.progressPercent && hasProgress == other.hasProgress;
  }
};

class ReadingStatsStore {
 public:
  static constexpr uint8_t FILE_VERSION = 1;
  static constexpr size_t MAX_RECORDS = 64;
  static constexpr size_t MAX_PATH_BYTES = 1023;

  static ReadingStatsStore& instance();

  static bool applySession(std::vector<ReadingStatsRecord>& records, const std::string& path, uint32_t seconds,
                           bool hasProgress, uint8_t progressPercent);
  static std::vector<uint8_t> serializeRecords(const std::vector<ReadingStatsRecord>& records);
  static bool deserializeRecords(const uint8_t* data, size_t len, std::vector<ReadingStatsRecord>& out);

  bool load();
  bool save() const;
  bool recordSession(const std::string& path, uint32_t seconds, bool hasProgress, uint8_t progressPercent);
  const ReadingStatsRecord* find(const std::string& path) const;
  const std::vector<ReadingStatsRecord>& records() const { return records_; }

 private:
  ReadingStatsStore() = default;

  std::vector<ReadingStatsRecord> records_;
  bool loaded_ = false;
};

}  // namespace papyrix

#define READING_STATS papyrix::ReadingStatsStore::instance()
