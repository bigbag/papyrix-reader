#include "ZipFile.h"

#include <BuildArena.h>
#include <Logging.h>

#define TAG "ZIP"
#include <InflateReader.h>
#include <SDCardManager.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <climits>
#include <cstddef>

struct ZipInflateCtx {
  InflateReader reader;  // Must be first — callback casts uzlib_uncomp* to ZipInflateCtx*
  FsFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};

static_assert(offsetof(ZipInflateCtx, reader) == 0, "reader must be at offset 0 for the uzlib callback cast to work");

namespace {
constexpr uint16_t ZIP_METHOD_STORED = 0;
constexpr uint16_t ZIP_METHOD_DEFLATED = 8;
constexpr uint16_t MAX_ZIP_ENTRIES = 10000;
constexpr size_t CENTRAL_HEADER_SIZE = 46;

enum class CentralEntryResult { Ok, End, Skip, Invalid };

uint16_t readLe16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8)); }

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

CentralEntryResult readCentralEntry(FsFile& file, ZipFile::FileStatSlim& stat, char* itemName, size_t itemNameSize) {
  uint8_t header[CENTRAL_HEADER_SIZE];
  if (file.read(header, sizeof(header)) != sizeof(header)) return CentralEntryResult::Invalid;
  if (readLe32(header) != 0x02014b50) return CentralEntryResult::End;

  stat.method = readLe16(header + 10);
  stat.compressedSize = readLe32(header + 20);
  stat.uncompressedSize = readLe32(header + 24);
  const uint16_t nameLen = readLe16(header + 28);
  const uint16_t extraLen = readLe16(header + 30);
  const uint16_t commentLen = readLe16(header + 32);
  stat.localHeaderOffset = readLe32(header + 42);

  const size_t variableSize = static_cast<size_t>(nameLen) + extraLen + commentLen;
  const size_t fileSize = file.size();
  const size_t position = file.position();
  if (position > fileSize || variableSize > fileSize - position) return CentralEntryResult::Invalid;
  if (nameLen >= itemNameSize) {
    return file.seekCur(variableSize) ? CentralEntryResult::Skip : CentralEntryResult::Invalid;
  }
  if (file.read(itemName, nameLen) != nameLen) return CentralEntryResult::Invalid;
  itemName[nameLen] = '\0';
  if (!file.seekCur(static_cast<size_t>(extraLen) + commentLen)) return CentralEntryResult::Invalid;
  return CentralEntryResult::Ok;
}

bool dataSpanFits(FsFile& file, size_t offset, size_t length) {
  const size_t fileSize = file.size();
  return offset <= fileSize && length <= fileSize - offset;
}

int zipReadCallback(uzlib_uncomp* uncomp) {
  auto* ctx = reinterpret_cast<ZipInflateCtx*>(uncomp);
  if (ctx->fileRemaining == 0) return -1;

  const size_t toRead = ctx->fileRemaining < ctx->readBufSize ? ctx->fileRemaining : ctx->readBufSize;
  const size_t bytesRead = ctx->file->read(ctx->readBuf, toRead);
  ctx->fileRemaining -= bytesRead;

  if (bytesRead == 0) return -1;

  uncomp->source = ctx->readBuf + 1;
  uncomp->source_limit = ctx->readBuf + bytesRead;
  return ctx->readBuf[0];
}
}  // namespace

bool ZipFile::loadAllFileStatSlims() {
  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return false;
  }

  if (!loadZipDetails()) {
    if (!wasOpen) {
      close();
    }
    return false;
  }

  if (!file.seek(zipDetails.centralDirOffset)) {
    if (!wasOpen) close();
    return false;
  }

  char itemName[256];
  fileStatSlimCache.clear();
  fileStatSlimCache.reserve(std::min<uint16_t>(zipDetails.totalEntries, 256));

  bool valid = true;
  for (uint16_t i = 0; i < zipDetails.totalEntries; i++) {
    FileStatSlim fileStat = {};
    const CentralEntryResult result = readCentralEntry(file, fileStat, itemName, sizeof(itemName));
    if (result == CentralEntryResult::Skip) continue;
    if (result != CentralEntryResult::Ok) {
      valid = false;
      break;
    }
    fileStatSlimCache.emplace(itemName, fileStat);
  }

  if (!valid) fileStatSlimCache.clear();
  if (!wasOpen) close();
  return valid;
}

bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* fileStat) {
  if (!fileStatSlimCache.empty()) {
    const auto it = fileStatSlimCache.find(filename);
    if (it != fileStatSlimCache.end()) {
      *fileStat = it->second;
      return true;
    }
    return false;
  }

  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return false;
  }

  if (!loadZipDetails()) {
    if (!wasOpen) {
      close();
    }
    return false;
  }

  if (!file.seek(zipDetails.centralDirOffset)) {
    if (!wasOpen) close();
    return false;
  }

  char itemName[256];
  bool found = false;

  for (uint16_t i = 0; i < zipDetails.totalEntries; i++) {
    FileStatSlim current = {};
    const CentralEntryResult result = readCentralEntry(file, current, itemName, sizeof(itemName));
    if (result == CentralEntryResult::Skip) continue;
    if (result != CentralEntryResult::Ok) break;
    if (strcmp(itemName, filename) == 0) {
      *fileStat = current;
      found = true;
      break;
    }
  }

  if (!wasOpen) close();
  return found;
}

long ZipFile::getDataOffset(const FileStatSlim& fileStat) {
  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return -1;
  }

  constexpr auto localHeaderSize = 30;

  uint8_t pLocalHeader[localHeaderSize];
  const uint64_t fileOffset = fileStat.localHeaderOffset;

  if (fileOffset > file.size() || !file.seek(fileOffset)) {
    if (!wasOpen) close();
    return -1;
  }
  const size_t read = file.read(pLocalHeader, localHeaderSize);
  if (!wasOpen) {
    close();
  }

  if (read != localHeaderSize) {
    LOG_ERR(TAG, "Something went wrong reading the local header");
    return -1;
  }

  if (pLocalHeader[0] + (pLocalHeader[1] << 8) + (pLocalHeader[2] << 16) + (pLocalHeader[3] << 24) !=
      0x04034b50 /* MZ_ZIP_LOCAL_DIR_HEADER_SIG */) {
    LOG_ERR(TAG, "Not a valid zip file header");
    return -1;
  }

  const uint16_t filenameLength = readLe16(pLocalHeader + 26);
  const uint16_t extraOffset = readLe16(pLocalHeader + 28);
  const uint64_t dataOffset = fileOffset + localHeaderSize + filenameLength + extraOffset;
  return dataOffset <= file.size() && dataOffset <= LONG_MAX ? static_cast<long>(dataOffset) : -1;
}

bool ZipFile::loadZipDetails() {
  if (zipDetails.isSet) {
    return true;
  }

  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return false;
  }

  const size_t fileSize = file.size();
  if (fileSize < 22) {
    LOG_ERR(TAG, "File too small to be a valid zip");
    if (!wasOpen) {
      close();
    }
    return false;  // Minimum EOCD size is 22 bytes
  }

  // We scan the last 1KB (or the whole file if smaller) for the EOCD signature
  // 0x06054b50 is stored as 0x50, 0x4b, 0x05, 0x06 in little-endian
  const int scanRange = fileSize > 1024 ? 1024 : fileSize;
  const auto buffer = static_cast<uint8_t*>(malloc(scanRange));
  if (!buffer) {
    LOG_ERR(TAG, "Failed to allocate memory for EOCD scan buffer");
    if (!wasOpen) {
      close();
    }
    return false;
  }

  if (!file.seek(fileSize - scanRange) || file.read(buffer, scanRange) != static_cast<size_t>(scanRange)) {
    LOG_ERR(TAG, "Failed to read EOCD scan range");
    free(buffer);
    if (!wasOpen) close();
    return false;
  }

  // Scan backwards for the signature
  int foundOffset = -1;
  for (int i = scanRange - 22; i >= 0; i--) {
    constexpr uint8_t signature[4] = {0x50, 0x4b, 0x05, 0x06};  // Little-endian EOCD signature
    if (memcmp(&buffer[i], signature, 4) == 0) {
      foundOffset = i;
      break;
    }
  }

  if (foundOffset == -1) {
    LOG_ERR(TAG, "EOCD signature not found in zip file");
    free(buffer);
    if (!wasOpen) {
      close();
    }
    return false;
  }

  // Now extract the values we need from the EOCD record
  // Relative positions within EOCD:
  // Offset 10: Total number of entries (2 bytes)
  // Offset 16: Offset of start of central directory with respect to the starting disk number (4 bytes)
  const uint16_t totalEntries = readLe16(&buffer[foundOffset + 10]);
  const uint32_t centralDirSize = readLe32(&buffer[foundOffset + 12]);
  const uint32_t centralDirOffset = readLe32(&buffer[foundOffset + 16]);
  const size_t eocdOffset = fileSize - scanRange + static_cast<size_t>(foundOffset);
  const bool validDirectory = totalEntries <= MAX_ZIP_ENTRIES && centralDirOffset <= eocdOffset &&
                              centralDirSize <= eocdOffset - centralDirOffset;
  if (!validDirectory) {
    LOG_ERR(TAG, "Invalid central directory: entries=%u offset=%u size=%u", totalEntries, centralDirOffset,
            centralDirSize);
    free(buffer);
    if (!wasOpen) close();
    return false;
  }

  zipDetails.totalEntries = totalEntries;
  zipDetails.centralDirOffset = centralDirOffset;
  zipDetails.isSet = true;

  free(buffer);
  if (!wasOpen) {
    close();
  }
  return true;
}

uint16_t ZipFile::getTotalEntries() {
  if (!zipDetails.isSet) {
    loadZipDetails();
  }
  return zipDetails.totalEntries;
}

bool ZipFile::open() {
  if (!SdMan.openFileForRead("ZIP", filePath, file)) {
    return false;
  }
  return true;
}

bool ZipFile::close() {
  if (file) {
    file.close();
  }
  return true;
}

bool ZipFile::getInflatedFileSize(const char* filename, size_t* size) {
  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  *size = static_cast<size_t>(fileStat.uncompressedSize);
  return true;
}

int ZipFile::fillUncompressedSizes(std::vector<SizeTarget>& targets, std::vector<uint32_t>& sizes) {
  if (targets.empty()) return 0;

  const bool wasOpen = isOpen();
  if ((!wasOpen && !open()) || !loadZipDetails() || !file.seek(zipDetails.centralDirOffset)) {
    if (!wasOpen) close();
    return 0;
  }

  char itemName[256];
  int matched = 0;
  for (uint16_t entry = 0; entry < zipDetails.totalEntries; entry++) {
    FileStatSlim stat = {};
    const CentralEntryResult result = readCentralEntry(file, stat, itemName, sizeof(itemName));
    if (result == CentralEntryResult::Skip) continue;
    if (result != CentralEntryResult::Ok) break;

    const size_t nameLen = strlen(itemName);
    const uint64_t entryHash = fnvHash64(itemName, nameLen);
    const SizeTarget key = {entryHash, static_cast<uint16_t>(nameLen), 0};
    auto it = std::lower_bound(targets.begin(), targets.end(), key);
    if (it != targets.end() && it->hash == entryHash && it->len == nameLen && it->index < sizes.size()) {
      sizes[it->index] = stat.uncompressedSize;
      matched++;
      if (matched >= static_cast<int>(targets.size())) break;
    }
  }

  if (!wasOpen) close();
  return matched;
}

int ZipFile::findFirstExisting(const char* const* paths, int pathCount) {
  if (!paths || pathCount <= 0 || pathCount > 65535) return -1;

  const bool wasOpen = isOpen();
  if ((!wasOpen && !open()) || !loadZipDetails()) {
    if (!wasOpen) close();
    return -1;
  }

  std::vector<SizeTarget> targets;
  for (int i = 0; i < pathCount; i++) {
    const char* path = paths[i];
    if (!path) continue;
    const size_t len = strlen(path);
    if (len > 255) continue;
    targets.push_back({fnvHash64(path, len), static_cast<uint16_t>(len), static_cast<uint16_t>(i)});
  }
  std::sort(targets.begin(), targets.end());

  if (!file.seek(zipDetails.centralDirOffset)) {
    if (!wasOpen) close();
    return -1;
  }

  char itemName[256];
  int foundIndex = -1;
  int lowestPriority = pathCount;
  for (uint16_t entry = 0; entry < zipDetails.totalEntries; entry++) {
    FileStatSlim stat = {};
    const CentralEntryResult result = readCentralEntry(file, stat, itemName, sizeof(itemName));
    if (result == CentralEntryResult::Skip) continue;
    if (result != CentralEntryResult::Ok) break;

    const size_t nameLen = strlen(itemName);
    const uint64_t entryHash = fnvHash64(itemName, nameLen);
    const SizeTarget key = {entryHash, static_cast<uint16_t>(nameLen), 0};
    auto it = std::lower_bound(targets.begin(), targets.end(), key);
    if (it != targets.end() && it->hash == entryHash && it->len == nameLen && it->index < pathCount &&
        strcmp(itemName, paths[it->index]) == 0 && it->index < lowestPriority) {
      lowestPriority = it->index;
      foundIndex = it->index;
      if (lowestPriority == 0) break;
    }
  }

  if (!wasOpen) close();
  return foundIndex;
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, const bool trailingNullByte) {
  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return nullptr;
  }

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    if (!wasOpen) {
      close();
    }
    return nullptr;
  }

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) {
    if (!wasOpen) {
      close();
    }
    return nullptr;
  }

  const size_t deflatedDataSize = fileStat.compressedSize;
  const size_t inflatedDataSize = fileStat.uncompressedSize;
  if (!dataSpanFits(file, static_cast<size_t>(fileOffset), deflatedDataSize) ||
      (fileStat.method == ZIP_METHOD_STORED && deflatedDataSize != inflatedDataSize) ||
      (trailingNullByte && inflatedDataSize == SIZE_MAX)) {
    LOG_ERR(TAG, "Invalid ZIP entry size");
    if (!wasOpen) close();
    return nullptr;
  }
  if (!file.seek(fileOffset)) {
    if (!wasOpen) close();
    return nullptr;
  }

  const size_t dataSize = inflatedDataSize + (trailingNullByte ? 1 : 0);
  const auto data = static_cast<uint8_t*>(malloc(dataSize));
  if (data == nullptr) {
    LOG_ERR(TAG, "Failed to allocate memory for output buffer (%zu bytes)", dataSize);
    if (!wasOpen) {
      close();
    }
    return nullptr;
  }

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const size_t dataRead = file.read(data, inflatedDataSize);
    if (!wasOpen) {
      close();
    }

    if (dataRead != inflatedDataSize) {
      LOG_ERR(TAG, "Failed to read data");
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else if (fileStat.method == ZIP_METHOD_DEFLATED) {
    // Read out deflated content from file
    const auto deflatedData = static_cast<uint8_t*>(malloc(deflatedDataSize));
    if (deflatedData == nullptr) {
      LOG_ERR(TAG, "Failed to allocate memory for decompression buffer");
      free(data);
      if (!wasOpen) {
        close();
      }
      return nullptr;
    }

    const size_t dataRead = file.read(deflatedData, deflatedDataSize);
    if (!wasOpen) {
      close();
    }

    if (dataRead != deflatedDataSize) {
      LOG_ERR(TAG, "Failed to read data, expected %d got %d", deflatedDataSize, dataRead);
      free(deflatedData);
      free(data);
      return nullptr;
    }

    bool success = false;
    {
      InflateReader r;
      r.init(false);
      r.setSource(deflatedData, deflatedDataSize);
      success = r.read(data, inflatedDataSize);
    }
    free(deflatedData);

    if (!success) {
      LOG_ERR(TAG, "Failed to inflate file");
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else {
    LOG_ERR(TAG, "Unsupported compression method");
    free(data);
    if (!wasOpen) {
      close();
    }
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize, uint8_t* dictBuffer,
                               const std::function<bool()>& shouldAbort, BuildArena* scratch) {
  return readFileToStreamDetailed(filename, out, chunkSize, dictBuffer, shouldAbort, scratch) ==
         StreamReadResult::Success;
}

StreamReadResult ZipFile::readFileToStreamDetailed(const char* filename, Print& out, const size_t chunkSize,
                                                   uint8_t* dictBuffer, const std::function<bool()>& shouldAbort,
                                                   BuildArena* scratch) {
  constexpr uint8_t YIELD_CHUNK_INTERVAL = 8;

  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return StreamReadResult::OpenFailed;
  }

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    if (!wasOpen) close();
    return StreamReadResult::NotFound;
  }

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) {
    if (!wasOpen) close();
    return StreamReadResult::InvalidOffset;
  }

  const size_t deflatedDataSize = fileStat.compressedSize;
  const size_t inflatedDataSize = fileStat.uncompressedSize;
  if (chunkSize == 0 || !dataSpanFits(file, static_cast<size_t>(fileOffset), deflatedDataSize) ||
      (fileStat.method == ZIP_METHOD_STORED && deflatedDataSize != inflatedDataSize) || !file.seek(fileOffset)) {
    if (!wasOpen) close();
    return StreamReadResult::InvalidOffset;
  }

  if (fileStat.method == ZIP_METHOD_STORED) {
    auto arenaScope = scratch ? scratch->scope() : BuildArena::Scope{};
    bool arenaBacked = false;
    uint8_t* buffer = scratch ? scratch->allocArray<uint8_t>(chunkSize) : nullptr;
    if (buffer) {
      arenaBacked = true;
    } else {
      if (scratch) {
        arenaScope.release();
        scratch->noteFallback(chunkSize);
      }
      buffer = static_cast<uint8_t*>(malloc(chunkSize));
    }
    if (!buffer) {
      LOG_ERR(TAG, "Failed to allocate memory for buffer");
      if (!wasOpen) close();
      return StreamReadResult::AllocFailed;
    }

    size_t remaining = inflatedDataSize;
    uint8_t chunkCounter = 0;
    while (remaining > 0) {
      if (shouldAbort && ++chunkCounter >= YIELD_CHUNK_INTERVAL) {
        chunkCounter = 0;
        if (shouldAbort()) {
          LOG_ERR(TAG, "Stored stream extraction aborted");
          if (!arenaBacked) free(buffer);
          if (!wasOpen) close();
          return StreamReadResult::Aborted;
        }
        delay(1);
      }

      const size_t dataRead = file.read(buffer, remaining < chunkSize ? remaining : chunkSize);
      if (dataRead == 0) {
        LOG_ERR(TAG, "Could not read more bytes");
        if (!arenaBacked) free(buffer);
        if (!wasOpen) close();
        return StreamReadResult::ReadError;
      }

      if (out.write(buffer, dataRead) != dataRead) {
        LOG_ERR(TAG, "Failed to write all output bytes to stream");
        if (!arenaBacked) free(buffer);
        if (!wasOpen) close();
        return StreamReadResult::WriteError;
      }
      remaining -= dataRead;
    }

    if (!wasOpen) close();
    if (!arenaBacked) free(buffer);
    return StreamReadResult::Success;
  }

  if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto arenaScope = scratch ? scratch->scope() : BuildArena::Scope{};
    bool arenaBacked = false;
    uint8_t* fileReadBuffer = nullptr;
    uint8_t* outputBuffer = nullptr;
    uint8_t* arenaDictionary = nullptr;

    size_t requiredBytes = SIZE_MAX;
    if (chunkSize <= (SIZE_MAX - InflateReader::STREAMING_DICTIONARY_SIZE) / 2) {
      requiredBytes = chunkSize * 2 + InflateReader::STREAMING_DICTIONARY_SIZE;
    }
    if (scratch && requiredBytes != SIZE_MAX) {
      fileReadBuffer = scratch->allocArray<uint8_t>(chunkSize);
      outputBuffer = scratch->allocArray<uint8_t>(chunkSize);
      arenaDictionary = scratch->allocArray<uint8_t>(InflateReader::STREAMING_DICTIONARY_SIZE);
      arenaBacked = fileReadBuffer && outputBuffer && arenaDictionary;
    }
    if (!arenaBacked) {
      if (scratch) {
        arenaScope.release();
        scratch->noteFallback(requiredBytes);
      }
      fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
      if (!fileReadBuffer) {
        LOG_ERR(TAG, "Failed to allocate memory for zip file read buffer");
        if (!wasOpen) close();
        return StreamReadResult::AllocFailed;
      }
      outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
      if (!outputBuffer) {
        free(fileReadBuffer);
        if (!wasOpen) close();
        return StreamReadResult::AllocFailed;
      }
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = chunkSize;

    if (!ctx.reader.init(true, arenaBacked ? arenaDictionary : dictBuffer)) {
      LOG_ERR(TAG, "Failed to init inflate reader (largest free: %u)",
              heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      if (!arenaBacked) {
        free(outputBuffer);
        free(fileReadBuffer);
      }
      if (!wasOpen) close();
      return StreamReadResult::AllocFailed;
    }
    ctx.reader.setReadCallback(zipReadCallback);

    StreamReadResult result = StreamReadResult::DecompressionError;
    size_t totalProduced = 0;
    uint8_t chunkCounter = 0;

    while (true) {
      if (shouldAbort && ++chunkCounter >= YIELD_CHUNK_INTERVAL) {
        chunkCounter = 0;
        if (shouldAbort()) {
          LOG_ERR(TAG, "Deflated stream extraction aborted");
          result = StreamReadResult::Aborted;
          break;
        }
        delay(1);
      }

      size_t produced;
      const InflateStatus status = ctx.reader.readAtMost(outputBuffer, chunkSize, &produced);

      totalProduced += produced;
      if (totalProduced > static_cast<size_t>(inflatedDataSize)) {
        LOG_ERR(TAG, "Decompressed size exceeds expected (%zu > %zu)", totalProduced,
                static_cast<size_t>(inflatedDataSize));
        result = StreamReadResult::SizeMismatch;
        break;
      }

      if (produced > 0) {
        if (out.write(outputBuffer, produced) != produced) {
          LOG_ERR(TAG, "Failed to write all output bytes to stream");
          result = StreamReadResult::WriteError;
          break;
        }
      }

      if (status == InflateStatus::Done) {
        if (totalProduced != static_cast<size_t>(inflatedDataSize)) {
          LOG_ERR(TAG, "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(inflatedDataSize),
                  totalProduced);
          result = StreamReadResult::SizeMismatch;
          break;
        }
        LOG_DBG(TAG, "Decompressed %d bytes into %d bytes", deflatedDataSize, inflatedDataSize);
        result = StreamReadResult::Success;
        break;
      }

      if (status == InflateStatus::Error) {
        LOG_ERR(TAG, "Decompression failed");
        result = StreamReadResult::DecompressionError;
        break;
      }
    }

    if (!wasOpen) close();
    if (!arenaBacked) {
      free(outputBuffer);
      free(fileReadBuffer);
    }
    return result;
  }

  if (!wasOpen) close();
  LOG_ERR(TAG, "Unsupported compression method");
  return StreamReadResult::UnsupportedMethod;
}
