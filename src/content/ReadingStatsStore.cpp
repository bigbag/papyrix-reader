#include "ReadingStatsStore.h"

#include <Logging.h>
#include <SDCardManager.h>
#include <SdFat.h>

#ifndef TEST_BUILD
#include <esp_heap_caps.h>
#endif

#include <algorithm>
#include <climits>
#include <utility>

#define TAG "READ_STATS"

namespace papyrix {
namespace {

constexpr char STATS_FILE[] = "/.papyrix/reading-stats.bin";
constexpr char STATS_TMP[] = "/.papyrix/reading-stats.bin.tmp";
constexpr size_t MAX_FILE_SIZE =
    2 + ReadingStatsStore::MAX_RECORDS * (2 + ReadingStatsStore::MAX_PATH_BYTES + 4 + 4 + 1 + 1);

uint32_t saturatingAdd(uint32_t a, uint32_t b) { return UINT32_MAX - a < b ? UINT32_MAX : a + b; }

void appendU16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 24));
}

bool readU16(const uint8_t* data, size_t len, size_t& pos, uint16_t& value) {
  if (pos + 2 > len) return false;
  value = static_cast<uint16_t>(data[pos]) | static_cast<uint16_t>(data[pos + 1] << 8);
  pos += 2;
  return true;
}

bool readU32(const uint8_t* data, size_t len, size_t& pos, uint32_t& value) {
  if (pos + 4 > len) return false;
  value = static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
          (static_cast<uint32_t>(data[pos + 2]) << 16) | (static_cast<uint32_t>(data[pos + 3]) << 24);
  pos += 4;
  return true;
}

}  // namespace

ReadingStatsStore& ReadingStatsStore::instance() {
  static ReadingStatsStore store;
  return store;
}

std::vector<ReadingStatsRecord> ReadingStatsStore::applySession(std::vector<ReadingStatsRecord> records,
                                                                const std::string& path, uint32_t seconds,
                                                                bool hasProgress, uint8_t progressPercent) {
  if (path.empty() || path.size() > MAX_PATH_BYTES || (seconds == 0 && !hasProgress)) return records;

  ReadingStatsRecord updated;
  const auto existing = std::find_if(records.begin(), records.end(),
                                     [&path](const ReadingStatsRecord& record) { return record.path == path; });
  if (existing != records.end()) {
    updated = std::move(*existing);
    records.erase(existing);
  } else {
    updated.path = path;
  }

  if (seconds > 0) {
    updated.totalSeconds = saturatingAdd(updated.totalSeconds, seconds);
    updated.sessionCount = saturatingAdd(updated.sessionCount, 1);
  }
  if (hasProgress) {
    updated.hasProgress = true;
    updated.progressPercent = std::min<uint8_t>(progressPercent, 100);
  }

  records.insert(records.begin(), std::move(updated));
  if (records.size() > MAX_RECORDS) records.resize(MAX_RECORDS);
  return records;
}

std::vector<uint8_t> ReadingStatsStore::serializeRecords(const std::vector<ReadingStatsRecord>& records) {
  const size_t count = std::min(records.size(), MAX_RECORDS);
  size_t reserveSize = 2;
  for (size_t i = 0; i < count; ++i) {
    if (records[i].path.empty() || records[i].path.size() > MAX_PATH_BYTES) return {};
    reserveSize += 2 + records[i].path.size() + 4 + 4 + 1 + 1;
  }

  std::vector<uint8_t> out;
  out.reserve(reserveSize);
  out.push_back(FILE_VERSION);
  out.push_back(static_cast<uint8_t>(count));
  for (size_t i = 0; i < count; ++i) {
    const auto& record = records[i];
    appendU16(out, static_cast<uint16_t>(record.path.size()));
    out.insert(out.end(), record.path.begin(), record.path.end());
    appendU32(out, record.totalSeconds);
    appendU32(out, record.sessionCount);
    out.push_back(record.hasProgress ? 1 : 0);
    out.push_back(record.progressPercent);
  }
  return out;
}

bool ReadingStatsStore::deserializeRecords(const uint8_t* data, size_t len, std::vector<ReadingStatsRecord>& out) {
  out.clear();
  if (!data || len < 2 || data[0] != FILE_VERSION || data[1] > MAX_RECORDS) return false;

  const uint8_t count = data[1];
  size_t pos = 2;
  out.reserve(count);
  for (uint8_t i = 0; i < count; ++i) {
    uint16_t pathLen = 0;
    if (!readU16(data, len, pos, pathLen) || pathLen == 0 || pathLen > MAX_PATH_BYTES || pos + pathLen > len) {
      out.clear();
      return false;
    }

    ReadingStatsRecord record;
    record.path.assign(reinterpret_cast<const char*>(data + pos), pathLen);
    pos += pathLen;

    uint8_t flags = 0;
    if (!readU32(data, len, pos, record.totalSeconds) || !readU32(data, len, pos, record.sessionCount) ||
        pos + 2 > len) {
      out.clear();
      return false;
    }
    flags = data[pos++];
    record.progressPercent = data[pos++];
    if ((flags & ~uint8_t{1}) != 0 || ((flags & 1) != 0 && record.progressPercent > 100)) {
      out.clear();
      return false;
    }
    record.hasProgress = (flags & 1) != 0;
    out.push_back(std::move(record));
  }

  if (pos != len) {
    out.clear();
    return false;
  }
  return true;
}

bool ReadingStatsStore::load() {
  if (loaded_) return true;
  loaded_ = true;
  records_.clear();

  FsFile file;
  if (!SdMan.openFileForRead(TAG, STATS_FILE, file)) return true;

  const size_t len = file.size();
  if (len < 2 || len > MAX_FILE_SIZE) {
    LOG_ERR(TAG, "Bad stats file size %u", static_cast<unsigned>(len));
    file.close();
    return false;
  }
#ifndef TEST_BUILD
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < len) {
    LOG_ERR(TAG, "Stats file needs %u contiguous bytes", static_cast<unsigned>(len));
    file.close();
    return false;
  }
#endif

  std::vector<uint8_t> data(len);
  const int got = file.read(data.data(), len);
  file.close();
  if (got != static_cast<int>(len) || !deserializeRecords(data.data(), data.size(), records_)) {
    LOG_ERR(TAG, "Corrupt or incomplete reading stats file");
    records_.clear();
    return false;
  }
  return true;
}

bool ReadingStatsStore::save() const {
  const std::vector<uint8_t> data = serializeRecords(records_);
  if (data.empty()) {
    LOG_ERR(TAG, "Failed to serialize reading stats");
    return false;
  }

  SdMan.mkdir("/.papyrix");
  FsFile file;
  if (!SdMan.openFileForWrite(TAG, STATS_TMP, file)) {
    LOG_ERR(TAG, "Failed to open reading stats temp file");
    return false;
  }

  const size_t written = file.write(data.data(), data.size());
  file.sync();
  file.close();
  if (written != data.size()) {
    LOG_ERR(TAG, "Short stats write %u/%u", static_cast<unsigned>(written), static_cast<unsigned>(data.size()));
    SdMan.remove(STATS_TMP);
    return false;
  }
  if (!SdMan.commitFile(STATS_TMP, STATS_FILE)) {
    LOG_ERR(TAG, "Failed to commit reading stats");
    return false;
  }
  return true;
}

bool ReadingStatsStore::recordSession(const std::string& path, uint32_t seconds, bool hasProgress,
                                      uint8_t progressPercent) {
  if (!loaded_) load();
  auto updated = applySession(records_, path, seconds, hasProgress, progressPercent);
  if (updated == records_) return false;
  records_ = std::move(updated);
  return true;
}

const ReadingStatsRecord* ReadingStatsStore::find(const std::string& path) const {
  const auto it = std::find_if(records_.begin(), records_.end(),
                               [&path](const ReadingStatsRecord& record) { return record.path == path; });
  return it == records_.end() ? nullptr : &*it;
}

}  // namespace papyrix
