#include "FileIndex.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <esp_heap_caps.h>
#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace {
constexpr char INDEX_DIRECTORY[] = "/.papyrix/fileindex";
constexpr char MAGIC[4] = {'P', 'F', 'I', 'X'};
constexpr uint8_t VERSION = 1;
constexpr size_t CHUNK_RECORDS = 16;
constexpr uint8_t DIRECTORY_FLAG = 1;
constexpr uint32_t FNV32_BASIS = 2166136261u;

uint32_t fnv1a32(const void* data, size_t length, uint32_t hash) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length; i++) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

uint64_t fnv1a64(const char* value) {
  uint64_t hash = 1469598103934665603ULL;
  while (*value) {
    hash ^= static_cast<uint8_t>(*value++);
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool readExact(FsFile& file, void* data, size_t length) {
  return file.read(static_cast<uint8_t*>(data), length) == static_cast<int>(length);
}

bool writeExact(FsFile& file, const void* data, size_t length) {
  return file.write(static_cast<const uint8_t*>(data), length) == length;
}

void maybeYield(uint32_t& counter) {
  if ((++counter & 0xFFu) == 0) delay(1);
}
}  // namespace

struct FileIndex::BuildState {
  FsFile runsOut;
  std::unique_ptr<Record[]> chunk;
  size_t chunkUsed = 0;
  uint32_t runCount = 0;
  uint32_t yieldCounter = 0;
  Record left{};
  Record right{};
  char temporaryPath[72] = {};
  char runsPathA[72] = {};
  char runsPathB[72] = {};
};

bool FileIndex::open(const char* directory, AcceptFn accept) {
  close();
  if (!directory || directory[0] != '/' || !accept) return false;

  const size_t pathLength = strlen(directory);
  if (pathLength == 0 || pathLength > UINT16_MAX) return false;

  const int pathResult = snprintf(indexPath_, sizeof(indexPath_), "%s/%016llx.idx", INDEX_DIRECTORY,
                                  static_cast<unsigned long long>(fnv1a64(directory)));
  if (pathResult < 0 || static_cast<size_t>(pathResult) >= sizeof(indexPath_)) return false;

  uint32_t signature = 0;
  uint32_t count = 0;
  if (!scanDirectory(directory, accept, signature, count)) return false;
  if (loadExisting(directory, signature, count)) return true;

  LOG_INF("FIDX", "Building index for %s (%u entries)", directory, count);
  return build(directory, accept);
}

void FileIndex::close() {
  if (indexFile_) indexFile_.close();
  opened_ = false;
  memset(&header_, 0, sizeof(header_));
}

bool FileIndex::scanDirectory(const char* directory, AcceptFn accept, uint32_t& signature, uint32_t& count) {
  FsFile root = SdMan.open(directory);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return false;
  }

  const size_t pathLength = strlen(directory);
  const uint64_t maximumRecords = (UINT32_MAX - sizeof(Header) - pathLength) / sizeof(Record);
  uint32_t hash = FNV32_BASIS;
  uint32_t entries = 0;
  uint32_t yieldCounter = 0;

  while (true) {
    FsFile entry = root.openNextFile();
    if (!entry) break;

    entry.getName(nameBuffer_, sizeof(nameBuffer_));
    const bool isDirectory = entry.isDirectory();
    entry.close();
    if (nameBuffer_[0] == '\0' || !accept(nameBuffer_, isDirectory)) continue;
    if (entries >= maximumRecords) {
      root.close();
      LOG_ERR("FIDX", "Directory is too large to index");
      return false;
    }

    const uint16_t nameLength = static_cast<uint16_t>(strlen(nameBuffer_));
    const uint8_t flags = isDirectory ? DIRECTORY_FLAG : 0;
    hash = fnv1a32(&nameLength, sizeof(nameLength), hash);
    hash = fnv1a32(nameBuffer_, nameLength, hash);
    hash = fnv1a32(&flags, sizeof(flags), hash);
    entries++;
    maybeYield(yieldCounter);
  }
  root.close();

  signature = hash;
  count = entries;
  return true;
}

bool FileIndex::loadExisting(const char* directory, uint32_t signature, uint32_t count) {
  FsFile file = SdMan.open(indexPath_);
  if (!file) return false;

  Header candidate{};
  const size_t pathLength = strlen(directory);
  bool valid = readExact(file, &candidate, sizeof(candidate)) && memcmp(candidate.magic, MAGIC, sizeof(MAGIC)) == 0 &&
               candidate.version == VERSION && candidate.reserved == 0 && candidate.pathLength == pathLength &&
               candidate.directorySignature == signature && candidate.entryCount == count &&
               candidate.recordsOffset == sizeof(Header) + pathLength;

  if (valid) {
    const uint64_t expectedSize =
        static_cast<uint64_t>(candidate.recordsOffset) + static_cast<uint64_t>(candidate.entryCount) * sizeof(Record);
    valid = expectedSize <= UINT32_MAX && static_cast<uint64_t>(file.fileSize()) == expectedSize;
  }

  size_t compared = 0;
  while (valid && compared < pathLength) {
    const size_t bytes = std::min(sizeof(nameBuffer_), pathLength - compared);
    valid = readExact(file, nameBuffer_, bytes) && memcmp(nameBuffer_, directory + compared, bytes) == 0;
    compared += bytes;
  }

  uint32_t recordsChecksum = FNV32_BASIS;
  Record record{};
  for (uint32_t row = 0; valid && row < candidate.entryCount; row++) {
    valid = readExact(file, &record, sizeof(record)) && (record.flags & ~DIRECTORY_FLAG) == 0 &&
            memchr(record.name, '\0', sizeof(record.name)) != nullptr;
    if (valid) recordsChecksum = fnv1a32(&record, sizeof(record), recordsChecksum);
  }
  valid = valid && recordsChecksum == candidate.recordsChecksum;

  if (!valid) {
    file.close();
    return false;
  }

  header_ = candidate;
  indexFile_ = std::move(file);
  opened_ = true;
  return true;
}

int FileIndex::compareRecords(const Record& left, const Record& right) {
  const bool leftDirectory = (left.flags & DIRECTORY_FLAG) != 0;
  const bool rightDirectory = (right.flags & DIRECTORY_FLAG) != 0;
  if (leftDirectory != rightDirectory) return leftDirectory ? -1 : 1;
  return FsHelpers::naturalCompare(left.name, right.name);
}

bool FileIndex::flushChunk(BuildState& state) {
  for (size_t i = 1; i < state.chunkUsed; i++) {
    const Record candidate = state.chunk[i];
    size_t position = i;
    while (position > 0 && compareRecords(candidate, state.chunk[position - 1]) < 0) {
      state.chunk[position] = state.chunk[position - 1];
      position--;
    }
    state.chunk[position] = candidate;
  }

  const size_t bytes = state.chunkUsed * sizeof(Record);
  if (bytes > 0 && !writeExact(state.runsOut, state.chunk.get(), bytes)) return false;
  state.chunkUsed = 0;
  state.runCount++;
  return true;
}

bool FileIndex::mergeRuns(BuildState& state, uint32_t recordCount, const char*& finalPath) {
  const char* inputPath = state.runsPathA;
  const char* outputPath = state.runsPathB;
  uint32_t runLength = CHUNK_RECORDS;
  uint32_t runCount = state.runCount;

  while (runCount > 1) {
    FsFile inputA = SdMan.open(inputPath);
    FsFile inputB = SdMan.open(inputPath);
    FsFile output = SdMan.open(outputPath, O_RDWR | O_CREAT | O_TRUNC);
    if (!inputA || !inputB || !output) {
      if (inputA) inputA.close();
      if (inputB) inputB.close();
      if (output) output.close();
      return false;
    }

    bool success = true;
    uint32_t mergedRuns = 0;
    for (uint32_t run = 0; success && run < runCount; run += 2) {
      const uint32_t startA = run * runLength;
      const uint32_t lengthA = std::min(runLength, recordCount - startA);
      const uint64_t byteA = static_cast<uint64_t>(startA) * sizeof(Record);
      success = byteA <= UINT32_MAX && inputA.seekSet(static_cast<uint32_t>(byteA));

      if (run + 1 >= runCount) {
        uint32_t remaining = lengthA;
        while (success && remaining > 0) {
          const uint32_t batch = std::min<uint32_t>(remaining, CHUNK_RECORDS);
          const size_t bytes = static_cast<size_t>(batch) * sizeof(Record);
          success = readExact(inputA, state.chunk.get(), bytes) && writeExact(output, state.chunk.get(), bytes);
          remaining -= batch;
          maybeYield(state.yieldCounter);
        }
        mergedRuns++;
        break;
      }

      const uint32_t startB = startA + lengthA;
      const uint32_t lengthB = std::min(runLength, recordCount - startB);
      const uint64_t byteB = static_cast<uint64_t>(startB) * sizeof(Record);
      success = success && byteB <= UINT32_MAX && inputB.seekSet(static_cast<uint32_t>(byteB));

      uint32_t usedA = 0;
      uint32_t usedB = 0;
      bool haveA = false;
      bool haveB = false;
      while (success && (usedA < lengthA || usedB < lengthB)) {
        if (!haveA && usedA < lengthA) {
          success = readExact(inputA, &state.left, sizeof(Record));
          haveA = success;
        }
        if (success && !haveB && usedB < lengthB) {
          success = readExact(inputB, &state.right, sizeof(Record));
          haveB = success;
        }
        if (!success) break;

        const bool takeA = haveA && (!haveB || compareRecords(state.left, state.right) <= 0);
        const Record& selected = takeA ? state.left : state.right;
        success = writeExact(output, &selected, sizeof(selected));
        if (takeA) {
          haveA = false;
          usedA++;
        } else {
          haveB = false;
          usedB++;
        }
        maybeYield(state.yieldCounter);
      }
      mergedRuns++;
    }

    inputA.close();
    inputB.close();
    if (success) success = output.sync();
    output.close();
    if (!success) return false;

    std::swap(inputPath, outputPath);
    runCount = mergedRuns;
    const uint64_t doubled = static_cast<uint64_t>(runLength) * 2;
    runLength = static_cast<uint32_t>(std::min<uint64_t>(recordCount, doubled));
  }

  finalPath = inputPath;
  return true;
}

bool FileIndex::build(const char* directory, AcceptFn accept) {
  if (!SdMan.ensureDirectoryExists(INDEX_DIRECTORY)) return false;

  const size_t allocationSize = sizeof(BuildState) + CHUNK_RECORDS * sizeof(Record);
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (allocationSize > largestBlock * 80 / 100) {
    LOG_ERR("FIDX", "Insufficient heap for index build");
    return false;
  }

  BuildState* stateRaw = new (std::nothrow) BuildState();
  Record* chunkRaw = new (std::nothrow) Record[CHUNK_RECORDS];
  if (!stateRaw || !chunkRaw) {
    delete stateRaw;
    delete[] chunkRaw;
    return false;
  }
  std::unique_ptr<BuildState> stateOwner(stateRaw);
  BuildState& state = *stateOwner;
  state.chunk.reset(chunkRaw);

  const int temporaryResult = snprintf(state.temporaryPath, sizeof(state.temporaryPath), "%s.tmp", indexPath_);
  const int runsAResult = snprintf(state.runsPathA, sizeof(state.runsPathA), "%s.a", indexPath_);
  const int runsBResult = snprintf(state.runsPathB, sizeof(state.runsPathB), "%s.b", indexPath_);
  if (temporaryResult < 0 || static_cast<size_t>(temporaryResult) >= sizeof(state.temporaryPath) || runsAResult < 0 ||
      static_cast<size_t>(runsAResult) >= sizeof(state.runsPathA) || runsBResult < 0 ||
      static_cast<size_t>(runsBResult) >= sizeof(state.runsPathB)) {
    return false;
  }

  auto cleanup = [&state]() {
    if (state.runsOut) state.runsOut.close();
    SdMan.remove(state.temporaryPath);
    SdMan.remove(state.runsPathA);
    SdMan.remove(state.runsPathB);
  };
  cleanup();

  state.runsOut = SdMan.open(state.runsPathA, O_RDWR | O_CREAT | O_TRUNC);
  FsFile root = SdMan.open(directory);
  if (!state.runsOut || !root || !root.isDirectory()) {
    if (root) root.close();
    cleanup();
    return false;
  }

  const size_t pathLength = strlen(directory);
  const uint64_t maximumRecords = (UINT32_MAX - sizeof(Header) - pathLength) / sizeof(Record);
  uint32_t signature = FNV32_BASIS;
  uint32_t recordCount = 0;
  bool success = true;
  while (success) {
    FsFile entry = root.openNextFile();
    if (!entry) break;

    entry.getName(nameBuffer_, sizeof(nameBuffer_));
    const bool isDirectory = entry.isDirectory();
    entry.close();
    if (nameBuffer_[0] == '\0' || !accept(nameBuffer_, isDirectory)) continue;
    if (recordCount >= maximumRecords) {
      success = false;
      break;
    }

    const uint16_t nameLength = static_cast<uint16_t>(strlen(nameBuffer_));
    const uint8_t flags = isDirectory ? DIRECTORY_FLAG : 0;
    signature = fnv1a32(&nameLength, sizeof(nameLength), signature);
    signature = fnv1a32(nameBuffer_, nameLength, signature);
    signature = fnv1a32(&flags, sizeof(flags), signature);

    Record& record = state.chunk[state.chunkUsed++];
    memset(&record, 0, sizeof(record));
    record.flags = flags;
    memcpy(record.name, nameBuffer_, nameLength);
    recordCount++;

    if (state.chunkUsed == CHUNK_RECORDS) success = flushChunk(state);
    maybeYield(state.yieldCounter);
  }
  root.close();
  if (success && state.chunkUsed > 0) success = flushChunk(state);
  if (success) success = state.runsOut.sync();
  state.runsOut.close();
  if (!success) {
    cleanup();
    return false;
  }

  const char* finalRunsPath = nullptr;
  success = mergeRuns(state, recordCount, finalRunsPath);
  FsFile sortedRuns;
  FsFile temporary;
  if (success) {
    sortedRuns = SdMan.open(finalRunsPath);
    temporary = SdMan.open(state.temporaryPath, O_RDWR | O_CREAT | O_TRUNC);
    success = static_cast<bool>(sortedRuns) && static_cast<bool>(temporary);
  }

  Header newHeader{};
  memcpy(newHeader.magic, MAGIC, sizeof(MAGIC));
  newHeader.version = VERSION;
  newHeader.pathLength = static_cast<uint16_t>(pathLength);
  newHeader.directorySignature = signature;
  newHeader.entryCount = recordCount;
  newHeader.recordsOffset = sizeof(Header) + pathLength;

  if (success)
    success = writeExact(temporary, &newHeader, sizeof(newHeader)) && writeExact(temporary, directory, pathLength);

  uint32_t checksum = FNV32_BASIS;
  for (uint32_t row = 0; success && row < recordCount; row++) {
    success = readExact(sortedRuns, &state.left, sizeof(Record)) && writeExact(temporary, &state.left, sizeof(Record));
    if (success) checksum = fnv1a32(&state.left, sizeof(Record), checksum);
    maybeYield(state.yieldCounter);
  }
  newHeader.recordsChecksum = checksum;
  if (success) success = temporary.seekSet(0) && writeExact(temporary, &newHeader, sizeof(newHeader));

  if (sortedRuns) sortedRuns.close();
  if (temporary) {
    if (success) success = temporary.sync();
    temporary.close();
  }
  if (!success || !SdMan.commitFile(state.temporaryPath, indexPath_)) {
    cleanup();
    return false;
  }

  SdMan.remove(state.runsPathA);
  SdMan.remove(state.runsPathB);
  indexFile_ = SdMan.open(indexPath_);
  if (!indexFile_) return false;

  header_ = newHeader;
  opened_ = true;
  return true;
}

bool FileIndex::readRecordAt(size_t row, Record& record) {
  if (!opened_ || row >= header_.entryCount) return false;
  const uint64_t offset = static_cast<uint64_t>(header_.recordsOffset) + static_cast<uint64_t>(row) * sizeof(Record);
  if (offset > UINT32_MAX || !indexFile_.seekSet(static_cast<uint32_t>(offset)) ||
      !readExact(indexFile_, &record, sizeof(record))) {
    return false;
  }
  return (record.flags & ~DIRECTORY_FLAG) == 0 && memchr(record.name, '\0', sizeof(record.name)) != nullptr;
}

bool FileIndex::entryAt(size_t row, Entry& out) {
  Record record{};
  if (!readRecordAt(row, record)) return false;
  memcpy(out.name, record.name, sizeof(out.name));
  out.name[MAX_NAME] = '\0';
  out.isDir = (record.flags & DIRECTORY_FLAG) != 0;
  return true;
}

size_t FileIndex::findRowByName(const char* name) {
  if (!opened_ || !name || !indexFile_.seekSet(header_.recordsOffset)) return SIZE_MAX;

  Record record{};
  for (size_t row = 0; row < header_.entryCount; row++) {
    if (!readExact(indexFile_, &record, sizeof(record)) || (record.flags & ~DIRECTORY_FLAG) != 0 ||
        memchr(record.name, '\0', sizeof(record.name)) == nullptr) {
      return SIZE_MAX;
    }
    if (strcasecmp(record.name, name) == 0) return row;
    if ((row & 0xFFu) == 0xFFu) delay(1);
  }
  return SIZE_MAX;
}
