#include "ZipFile.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <miniz.h>

#include <algorithm>

bool inflateOneShot(const uint8_t* inputBuf, const size_t deflatedSize, uint8_t* outputBuf, const size_t inflatedSize) {
  // Setup inflator
  const auto inflator = static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
  if (!inflator) {
    Serial.printf("[%lu] [ZIP] Failed to allocate memory for inflator\n", millis());
    return false;
  }
  memset(inflator, 0, sizeof(tinfl_decompressor));
  tinfl_init(inflator);

  size_t inBytes = deflatedSize;
  size_t outBytes = inflatedSize;
  const tinfl_status status = tinfl_decompress(inflator, inputBuf, &inBytes, nullptr, outputBuf, &outBytes,
                                               TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  free(inflator);

  if (status != TINFL_STATUS_DONE) {
    Serial.printf("[%lu] [ZIP] tinfl_decompress() failed with status %d\n", millis(), status);
    return false;
  }

  return true;
}

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

  file.seek(zipDetails.centralDirOffset);

  uint32_t sig;
  char itemName[256];
  fileStatSlimCache.clear();
  fileStatSlimCache.reserve(zipDetails.totalEntries);

  while (file.available()) {
    file.read(&sig, 4);
    if (sig != 0x02014b50) break;  // End of list

    FileStatSlim fileStat = {};

    file.seekCur(6);
    file.read(&fileStat.method, 2);
    file.seekCur(8);
    file.read(&fileStat.compressedSize, 4);
    file.read(&fileStat.uncompressedSize, 4);
    uint16_t nameLen, m, k;
    file.read(&nameLen, 2);
    file.read(&m, 2);
    file.read(&k, 2);
    file.seekCur(8);
    file.read(&fileStat.localHeaderOffset, 4);

    // Bounds check to prevent buffer overflow
    if (nameLen >= 255) {
      file.seekCur(nameLen + m + k);  // Skip this entry entirely
      continue;
    }

    file.read(itemName, nameLen);
    itemName[nameLen] = '\0';

    fileStatSlimCache.emplace(itemName, fileStat);

    // Skip the rest of this entry (extra field + comment)
    file.seekCur(m + k);
  }

  if (!wasOpen) {
    close();
  }
  return true;
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

  file.seek(zipDetails.centralDirOffset);

  uint32_t sig;
  char itemName[256];
  bool found = false;

  while (file.available()) {
    file.read(&sig, 4);
    if (sig != 0x02014b50) break;  // End of list

    file.seekCur(6);
    file.read(&fileStat->method, 2);
    file.seekCur(8);
    file.read(&fileStat->compressedSize, 4);
    file.read(&fileStat->uncompressedSize, 4);
    uint16_t nameLen, m, k;
    file.read(&nameLen, 2);
    file.read(&m, 2);
    file.read(&k, 2);
    file.seekCur(8);
    file.read(&fileStat->localHeaderOffset, 4);

    // Bounds check to prevent buffer overflow
    if (nameLen >= 255) {
      file.seekCur(nameLen + m + k);  // Skip this entry entirely
      continue;
    }

    file.read(itemName, nameLen);
    itemName[nameLen] = '\0';

    if (strcmp(itemName, filename) == 0) {
      found = true;
      break;
    }

    // Skip the rest of this entry (extra field + comment)
    file.seekCur(m + k);
  }

  if (!wasOpen) {
    close();
  }
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

  file.seek(fileOffset);
  const size_t read = file.read(pLocalHeader, localHeaderSize);
  if (!wasOpen) {
    close();
  }

  if (read != localHeaderSize) {
    Serial.printf("[%lu] [ZIP] Something went wrong reading the local header\n", millis());
    return -1;
  }

  if (pLocalHeader[0] + (pLocalHeader[1] << 8) + (pLocalHeader[2] << 16) + (pLocalHeader[3] << 24) !=
      0x04034b50 /* MZ_ZIP_LOCAL_DIR_HEADER_SIG */) {
    Serial.printf("[%lu] [ZIP] Not a valid zip file header\n", millis());
    return -1;
  }

  const uint16_t filenameLength = pLocalHeader[26] + (pLocalHeader[27] << 8);
  const uint16_t extraOffset = pLocalHeader[28] + (pLocalHeader[29] << 8);
  return fileOffset + localHeaderSize + filenameLength + extraOffset;
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
    Serial.printf("[%lu] [ZIP] File too small to be a valid zip\n", millis());
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
    Serial.printf("[%lu] [ZIP] Failed to allocate memory for EOCD scan buffer\n", millis());
    if (!wasOpen) {
      close();
    }
    return false;
  }

  file.seek(fileSize - scanRange);
  file.read(buffer, scanRange);

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
    Serial.printf("[%lu] [ZIP] EOCD signature not found in zip file\n", millis());
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
  memcpy(&zipDetails.totalEntries, &buffer[foundOffset + 10], sizeof(zipDetails.totalEntries));
  memcpy(&zipDetails.centralDirOffset, &buffer[foundOffset + 16], sizeof(zipDetails.centralDirOffset));
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
  if (targets.empty()) {
    return 0;
  }

  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return 0;
  }

  if (!loadZipDetails()) {
    if (!wasOpen) {
      close();
    }
    return 0;
  }

  file.seek(zipDetails.centralDirOffset);

  uint32_t sig;
  char itemName[256];
  int matched = 0;

  while (file.available()) {
    if (file.read(&sig, 4) != 4) break;
    if (sig != 0x02014b50) break;  // End of central directory

    // Skip: version made by (2), version needed (2), flags (2), method (2), time (2), date (2), crc32 (4)
    file.seekCur(16);
    // Skip compressedSize (4), read uncompressedSize (4)
    file.seekCur(4);
    uint32_t uncompressedSize;
    if (file.read(&uncompressedSize, 4) != 4) break;
    uint16_t nameLen, m, k;
    if (file.read(&nameLen, 2) != 2) break;
    if (file.read(&m, 2) != 2) break;
    if (file.read(&k, 2) != 2) break;
    // Skip: comment len already read in k, disk# (2), internal attr (2), external attr (4), local header offset (4)
    file.seekCur(12);

    // Bounds check to prevent buffer overflow
    if (nameLen >= 255) {
      file.seekCur(nameLen + m + k);  // Skip this entry entirely
      continue;
    }

    if (file.read(itemName, nameLen) != nameLen) break;
    itemName[nameLen] = '\0';

    // Compute hash on-the-fly from filename
    const uint64_t entryHash = fnvHash64(itemName, nameLen);

    // Binary search for matching target
    SizeTarget key = {entryHash, nameLen, 0};
    auto it = std::lower_bound(targets.begin(), targets.end(), key);

    // Check for match (hash and len must match)
    if (it != targets.end() && it->hash == entryHash && it->len == nameLen) {
      // Bounds check before write
      if (it->index < sizes.size()) {
        sizes[it->index] = uncompressedSize;
        matched++;
      }
    }

    // Skip the rest of this entry (extra field + comment)
    file.seekCur(m + k);
  }

  if (!wasOpen) {
    close();
  }
  return matched;
}

int ZipFile::findFirstExisting(const char* const* paths, int pathCount) {
  if (!paths || pathCount <= 0 || pathCount > 65535) {
    return -1;
  }

  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return -1;
  }

  if (!loadZipDetails()) {
    if (!wasOpen) {
      close();
    }
    return -1;
  }

  // Build sorted vector of targets with hashes for binary search
  std::vector<SizeTarget> targets;
  for (int i = 0; i < pathCount; i++) {
    const char* path = paths[i];
    if (!path) continue;
    const size_t len = strlen(path);
    if (len > 255) continue;  // Skip paths that are too long for itemName[256]
    targets.push_back({fnvHash64(path, len), static_cast<uint16_t>(len), static_cast<uint16_t>(i)});
  }
  std::sort(targets.begin(), targets.end());

  file.seek(zipDetails.centralDirOffset);

  uint32_t sig;
  char itemName[256];
  int foundIndex = -1;
  int lowestPriority = pathCount;  // Lower index = higher priority

  while (file.available()) {
    if (file.read(&sig, 4) != 4) break;
    if (sig != 0x02014b50) break;  // End of central directory

    // Skip to name length (skip 24 bytes from after signature)
    if (!file.seekCur(24)) break;
    uint16_t nameLen, m, k;
    if (file.read(&nameLen, 2) != 2) break;
    if (file.read(&m, 2) != 2) break;
    if (file.read(&k, 2) != 2) break;
    // Skip remaining header (12 bytes)
    if (!file.seekCur(12)) break;

    // Bounds check to prevent buffer overflow
    if (nameLen > 255) {
      file.seekCur(nameLen + m + k);
      continue;
    }

    if (file.read(itemName, nameLen) != nameLen) break;
    itemName[nameLen] = '\0';

    // Compute hash on-the-fly from filename
    const uint64_t entryHash = fnvHash64(itemName, nameLen);

    // Binary search for matching target
    SizeTarget key = {entryHash, nameLen, 0};
    auto it = std::lower_bound(targets.begin(), targets.end(), key);

    // Check for match (hash and len must match)
    if (it != targets.end() && it->hash == entryHash && it->len == nameLen) {
      // Verify string match (hash collision protection) with bounds check
      if (it->index < pathCount && strcmp(itemName, paths[it->index]) == 0) {
        // Keep track of lowest index (highest priority)
        if (it->index < lowestPriority) {
          lowestPriority = it->index;
          foundIndex = it->index;
          if (lowestPriority == 0) break;  // Can't find higher priority
        }
      }
    }

    // Skip the rest of this entry (extra field + comment)
    file.seekCur(m + k);
  }

  if (!wasOpen) {
    close();
  }
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

  file.seek(fileOffset);

  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  const auto dataSize = trailingNullByte ? inflatedDataSize + 1 : inflatedDataSize;
  const auto data = static_cast<uint8_t*>(malloc(dataSize));
  if (data == nullptr) {
    Serial.printf("[%lu] [ZIP] Failed to allocate memory for output buffer (%zu bytes)\n", millis(), dataSize);
    if (!wasOpen) {
      close();
    }
    return nullptr;
  }

  if (fileStat.method == 0) {  // MZ_NO_COMPRESSION = 0
    // no deflation, just read content
    const size_t dataRead = file.read(data, inflatedDataSize);
    if (!wasOpen) {
      close();
    }

    if (dataRead != inflatedDataSize) {
      Serial.printf("[%lu] [ZIP] Failed to read data\n", millis());
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else if (fileStat.method == MZ_DEFLATED) {
    // Read out deflated content from file
    const auto deflatedData = static_cast<uint8_t*>(malloc(deflatedDataSize));
    if (deflatedData == nullptr) {
      Serial.printf("[%lu] [ZIP] Failed to allocate memory for decompression buffer\n", millis());
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
      Serial.printf("[%lu] [ZIP] Failed to read data, expected %d got %d\n", millis(), deflatedDataSize, dataRead);
      free(deflatedData);
      free(data);
      return nullptr;
    }

    const bool success = inflateOneShot(deflatedData, deflatedDataSize, data, inflatedDataSize);
    free(deflatedData);

    if (!success) {
      Serial.printf("[%lu] [ZIP] Failed to inflate file\n", millis());
      free(data);
      if (!wasOpen) close();
      return nullptr;
    }

    // Continue out of block with data set
  } else {
    Serial.printf("[%lu] [ZIP] Unsupported compression method\n", millis());
    if (!wasOpen) {
      close();
    }
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize) {
  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return false;
  }

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    if (!wasOpen) close();
    return false;
  }

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) {
    if (!wasOpen) close();
    return false;
  }

  file.seek(fileOffset);
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;

  if (fileStat.method == 0) {  // MZ_NO_COMPRESSION = 0
    // no deflation, just read content
    const auto buffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!buffer) {
      Serial.printf("[%lu] [ZIP] Failed to allocate memory for buffer\n", millis());
      if (!wasOpen) {
        close();
      }
      return false;
    }

    size_t remaining = inflatedDataSize;
    while (remaining > 0) {
      const size_t dataRead = file.read(buffer, remaining < chunkSize ? remaining : chunkSize);
      if (dataRead == 0) {
        Serial.printf("[%lu] [ZIP] Could not read more bytes\n", millis());
        free(buffer);
        if (!wasOpen) {
          close();
        }
        return false;
      }

      out.write(buffer, dataRead);
      remaining -= dataRead;
    }

    if (!wasOpen) {
      close();
    }
    free(buffer);
    return true;
  }

  if (fileStat.method == MZ_DEFLATED) {
    // Setup inflator
    const auto inflator = static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
    if (!inflator) {
      Serial.printf("[%lu] [ZIP] Failed to allocate memory for inflator\n", millis());
      if (!wasOpen) {
        close();
      }
      return false;
    }
    memset(inflator, 0, sizeof(tinfl_decompressor));
    tinfl_init(inflator);

    // Setup file read buffer
    const auto fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!fileReadBuffer) {
      Serial.printf("[%lu] [ZIP] Failed to allocate memory for zip file read buffer\n", millis());
      free(inflator);
      if (!wasOpen) {
        close();
      }
      return false;
    }

    const auto outputBuffer = static_cast<uint8_t*>(malloc(TINFL_LZ_DICT_SIZE));
    if (!outputBuffer) {
      Serial.printf("[%lu] [ZIP] Failed to allocate memory for dictionary\n", millis());
      free(inflator);
      free(fileReadBuffer);
      if (!wasOpen) {
        close();
      }
      return false;
    }
    memset(outputBuffer, 0, TINFL_LZ_DICT_SIZE);

    size_t fileRemainingBytes = deflatedDataSize;
    size_t processedOutputBytes = 0;
    size_t fileReadBufferFilledBytes = 0;
    size_t fileReadBufferCursor = 0;
    size_t outputCursor = 0;  // Current offset in the circular dictionary

    while (true) {
      // Load more compressed bytes when needed
      if (fileReadBufferCursor >= fileReadBufferFilledBytes) {
        if (fileRemainingBytes == 0) {
          // Should not be hit, but a safe protection
          break;  // EOF
        }

        fileReadBufferFilledBytes =
            file.read(fileReadBuffer, fileRemainingBytes < chunkSize ? fileRemainingBytes : chunkSize);
        fileRemainingBytes -= fileReadBufferFilledBytes;
        fileReadBufferCursor = 0;

        if (fileReadBufferFilledBytes == 0) {
          // Bad read
          break;  // EOF
        }
      }

      // Available bytes in fileReadBuffer to process
      size_t inBytes = fileReadBufferFilledBytes - fileReadBufferCursor;
      // Space remaining in outputBuffer
      size_t outBytes = TINFL_LZ_DICT_SIZE - outputCursor;

      const tinfl_status status = tinfl_decompress(inflator, fileReadBuffer + fileReadBufferCursor, &inBytes,
                                                   outputBuffer, outputBuffer + outputCursor, &outBytes,
                                                   fileRemainingBytes > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0);

      // Update input position
      fileReadBufferCursor += inBytes;

      // Write output chunk
      if (outBytes > 0) {
        processedOutputBytes += outBytes;
        if (out.write(outputBuffer + outputCursor, outBytes) != outBytes) {
          Serial.printf("[%lu] [ZIP] Failed to write all output bytes to stream\n", millis());
          if (!wasOpen) {
            close();
          }
          free(outputBuffer);
          free(fileReadBuffer);
          free(inflator);
          return false;
        }
        // Update output position in buffer (with wraparound)
        outputCursor = (outputCursor + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
      }

      if (status < 0) {
        Serial.printf("[%lu] [ZIP] tinfl_decompress() failed with status %d\n", millis(), status);
        if (!wasOpen) {
          close();
        }
        free(outputBuffer);
        free(fileReadBuffer);
        free(inflator);
        return false;
      }

      if (status == TINFL_STATUS_DONE) {
        Serial.printf("[%lu] [ZIP] Decompressed %d bytes into %d bytes\n", millis(), deflatedDataSize,
                      inflatedDataSize);
        if (!wasOpen) {
          close();
        }
        free(inflator);
        free(fileReadBuffer);
        free(outputBuffer);
        return true;
      }
    }

    // If we get here, EOF reached without TINFL_STATUS_DONE
    Serial.printf("[%lu] [ZIP] Unexpected EOF\n", millis());
    if (!wasOpen) {
      close();
    }
    free(outputBuffer);
    free(fileReadBuffer);
    free(inflator);
    return false;
  }

  if (!wasOpen) {
    close();
  }

  Serial.printf("[%lu] [ZIP] Unsupported compression method\n", millis());
  return false;
}
