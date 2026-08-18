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
  value = static_cast<uint16_t>(data[pos]) | (static_cast<uint16_t>(data[pos + 1]) << 8);
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

bool readExact(FsFile& file, void* destination, size_t size, size_t& remaining) {
  if (size > remaining || file.read(static_cast<uint8_t*>(destination), size) != static_cast<int>(size)) return false;
  remaining -= size;
  return true;
}

bool readU16(FsFile& file, uint16_t& value, size_t& remaining) {
  uint8_t bytes[2];
  if (!readExact(file, bytes, sizeof(bytes), remaining)) return false;
  value = static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
  return true;
}

bool readU32(FsFile& file, uint32_t& value, size_t& remaining) {
  uint8_t bytes[4];
  if (!readExact(file, bytes, sizeof(bytes), remaining)) return false;
  value = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
          (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
  return true;
}

bool writeExact(FsFile& file, const void* source, size_t size) {
  return file.write(static_cast<const uint8_t*>(source), size) == size;
}

bool writeU16(FsFile& file, uint16_t value) {
  const uint8_t bytes[] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  return writeExact(file, bytes, sizeof(bytes));
}

bool writeU32(FsFile& file, uint32_t value) {
  const uint8_t bytes[] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                           static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  return writeExact(file, bytes, sizeof(bytes));
}

bool hasDecodeHeadroom(size_t fileSize, size_t count) {
  if (count > SIZE_MAX / sizeof(ReadingStatsRecord)) return false;
  const size_t recordBytes = count * sizeof(ReadingStatsRecord);
  if (fileSize > SIZE_MAX - recordBytes) return false;
#ifndef TEST_BUILD
  const size_t estimatedBytes = fileSize + recordBytes;
  const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (estimatedBytes > freeBytes * 80 / 100 || (recordBytes > 1024 && recordBytes > largestBlock * 80 / 100)) {
    return false;
  }
#endif
  return true;
}

bool readRecords(FsFile& file, size_t fileSize, std::vector<ReadingStatsRecord>& out) {
  size_t remaining = fileSize;
  uint8_t header[2];
  if (!readExact(file, header, sizeof(header), remaining) || header[0] != ReadingStatsStore::FILE_VERSION ||
      header[1] > ReadingStatsStore::MAX_RECORDS || !hasDecodeHeadroom(fileSize, header[1])) {
    return false;
  }

  out.clear();
  out.reserve(header[1]);
  for (uint8_t i = 0; i < header[1]; ++i) {
    uint16_t pathLength = 0;
    if (!readU16(file, pathLength, remaining) || pathLength == 0 || pathLength > ReadingStatsStore::MAX_PATH_BYTES ||
        pathLength > remaining) {
      out.clear();
      return false;
    }

    ReadingStatsRecord record;
    record.path.resize(pathLength);
    uint8_t fields[2];
    if (!readExact(file, record.path.data(), pathLength, remaining) || !readU32(file, record.totalSeconds, remaining) ||
        !readU32(file, record.sessionCount, remaining) || !readExact(file, fields, sizeof(fields), remaining) ||
        (fields[0] & ~uint8_t{1}) != 0 || ((fields[0] & 1) != 0 && fields[1] > 100)) {
      out.clear();
      return false;
    }
    record.hasProgress = (fields[0] & 1) != 0;
    record.progressPercent = fields[1];
    out.push_back(std::move(record));
  }
  if (remaining != 0) {
    out.clear();
    return false;
  }
  return true;
}

bool writeRecords(FsFile& file, const std::vector<ReadingStatsRecord>& records) {
  const size_t count = std::min(records.size(), ReadingStatsStore::MAX_RECORDS);
  const uint8_t header[] = {ReadingStatsStore::FILE_VERSION, static_cast<uint8_t>(count)};
  if (!writeExact(file, header, sizeof(header))) return false;

  for (size_t i = 0; i < count; ++i) {
    const ReadingStatsRecord& record = records[i];
    if (record.path.empty() || record.path.size() > ReadingStatsStore::MAX_PATH_BYTES ||
        !writeU16(file, static_cast<uint16_t>(record.path.size())) ||
        !writeExact(file, record.path.data(), record.path.size()) || !writeU32(file, record.totalSeconds) ||
        !writeU32(file, record.sessionCount)) {
      return false;
    }
    const uint8_t fields[] = {static_cast<uint8_t>(record.hasProgress ? 1 : 0), record.progressPercent};
    if (!writeExact(file, fields, sizeof(fields))) return false;
  }
  return true;
}

}  // namespace

ReadingStatsStore& ReadingStatsStore::instance() {
  static ReadingStatsStore store;
  return store;
}

bool ReadingStatsStore::applySession(std::vector<ReadingStatsRecord>& records, const std::string& path,
                                     uint32_t seconds, bool hasProgress, uint8_t progressPercent) {
  if (path.empty() || path.size() > MAX_PATH_BYTES || (seconds == 0 && !hasProgress)) return false;

  auto existing = std::find_if(records.begin(), records.end(),
                               [&path](const ReadingStatsRecord& record) { return record.path == path; });
  bool changed = false;
  if (existing == records.end()) {
    if (records.size() >= MAX_RECORDS) records.resize(MAX_RECORDS - 1);
    records.insert(records.begin(), ReadingStatsRecord{});
    records.front().path = path;
    changed = true;
  } else if (existing != records.begin()) {
    std::rotate(records.begin(), existing, existing + 1);
    changed = true;
  }

  ReadingStatsRecord& updated = records.front();
  if (seconds > 0) {
    const uint32_t totalSeconds = saturatingAdd(updated.totalSeconds, seconds);
    const uint32_t sessionCount = saturatingAdd(updated.sessionCount, 1);
    changed = changed || totalSeconds != updated.totalSeconds || sessionCount != updated.sessionCount;
    updated.totalSeconds = totalSeconds;
    updated.sessionCount = sessionCount;
  }
  if (hasProgress) {
    const uint8_t clampedProgress = std::min<uint8_t>(progressPercent, 100);
    changed = changed || !updated.hasProgress || updated.progressPercent != clampedProgress;
    updated.hasProgress = true;
    updated.progressPercent = clampedProgress;
  }

  if (records.size() > MAX_RECORDS) {
    records.resize(MAX_RECORDS);
    changed = true;
  }

  return changed;
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
  const bool decoded = readRecords(file, len, records_);
  file.close();
  if (!decoded) {
    LOG_ERR(TAG, "Corrupt or incomplete reading stats file");
    std::vector<ReadingStatsRecord>().swap(records_);
    return false;
  }
  return true;
}

bool ReadingStatsStore::save() const {
  SdMan.mkdir("/.papyrix");
  FsFile file;
  if (!SdMan.openFileForWrite(TAG, STATS_TMP, file)) {
    LOG_ERR(TAG, "Failed to open reading stats temp file");
    return false;
  }

  const bool written = writeRecords(file, records_);
  const bool synced = written && file.sync();
  file.close();
  if (!synced) {
    LOG_ERR(TAG, "Failed stats write or sync");
    SdMan.remove(STATS_TMP);
    return false;
  }
  if (!SdMan.commitFile(STATS_TMP, STATS_FILE)) {
    LOG_ERR(TAG, "Failed to commit reading stats");
    SdMan.remove(STATS_TMP);
    return false;
  }
  return true;
}

bool ReadingStatsStore::recordSession(const std::string& path, uint32_t seconds, bool hasProgress,
                                      uint8_t progressPercent) {
  if (!loaded_) load();
  return applySession(records_, path, seconds, hasProgress, progressPercent);
}

const ReadingStatsRecord* ReadingStatsStore::find(const std::string& path) const {
  const auto it = std::find_if(records_.begin(), records_.end(),
                               [&path](const ReadingStatsRecord& record) { return record.path == path; });
  return it == records_.end() ? nullptr : &*it;
}

}  // namespace papyrix
