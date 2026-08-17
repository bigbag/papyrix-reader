#include "ProgressManager.h"

#include <Arduino.h>
#include <Logging.h>
#include <SdFat.h>
#include <Serialization.h>

#include <climits>
#include <cstdio>

#include "../content/ContentHandle.h"
#include "../core/Core.h"

#define TAG "PROGRESS"

namespace papyrix {

namespace {
constexpr uint32_t kProgressMagic = 0x47525050;  // "PPRG" in little-endian files
constexpr uint8_t kProgressVersion = 1;
constexpr uint32_t kProgressFileSize = 14;
}  // namespace

bool ProgressManager::save(Core& core, const char* cacheDir, ContentType type, const Progress& progress) {
  if (!cacheDir || cacheDir[0] == '\0') {
    return false;
  }

  char progressPath[280];
  snprintf(progressPath, sizeof(progressPath), "%s/progress.bin", cacheDir);
  char tmpPath[288];
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", progressPath);

  FsFile file;
  auto result = core.storage.openWrite(tmpPath, file);
  if (!result.ok()) {
    LOG_ERR(TAG, "Failed to open tmp progress %s", tmpPath);
    return false;
  }

  const int32_t spineIndex = static_cast<int32_t>(progress.spineIndex);
  const uint32_t page = type == ContentType::Xtc
                            ? progress.flatPage
                            : (progress.sectionPage > 0 ? static_cast<uint32_t>(progress.sectionPage) : 0);
  const uint8_t storedType = static_cast<uint8_t>(type);
  const bool writeOk =
      serialization::writePodChecked(file, kProgressMagic) && serialization::writePodChecked(file, kProgressVersion) &&
      serialization::writePodChecked(file, storedType) && serialization::writePodChecked(file, spineIndex) &&
      serialization::writePodChecked(file, page) && file.sync();
  if (writeOk) {
    if (type == ContentType::Epub || type == ContentType::Fb2) {
      LOG_DBG(TAG, "Saved %s: spine=%d page=%u", type == ContentType::Epub ? "EPUB" : "FB2", progress.spineIndex, page);
    } else {
      LOG_DBG(TAG, "Saved page %u", page);
    }
  }

  const uint32_t writtenBytes = file.size();
  file.close();
  if (!writeOk || writtenBytes != kProgressFileSize) {
    LOG_ERR(TAG, "Bad progress write; discarding tmp");
    core.storage.remove(tmpPath);
    return false;
  }
  if (!core.storage.commitFile(tmpPath, progressPath).ok()) {
    LOG_ERR(TAG, "Failed to commit progress file %s", progressPath);
    core.storage.remove(tmpPath);
    return false;
  }
  return true;
}

ProgressManager::Progress ProgressManager::load(Core& core, const char* cacheDir, ContentType type) {
  Progress progress;
  progress.reset();

  if (!cacheDir || cacheDir[0] == '\0') {
    return progress;
  }

  char progressPath[280];
  snprintf(progressPath, sizeof(progressPath), "%s/progress.bin", cacheDir);

  FsFile file;
  auto result = core.storage.openRead(progressPath, file);
  if (!result.ok()) {
    LOG_DBG(TAG, "No saved progress found");
    return progress;
  }

  const uint32_t fileSize = file.size();
  if (fileSize == 4) {
    uint8_t data[4];
    if (file.read(data, sizeof(data)) != sizeof(data)) {
      LOG_ERR(TAG, "Legacy progress read failed, using defaults");
      file.close();
      return progress;
    }
    if (type == ContentType::Epub || type == ContentType::Fb2) {
      progress.spineIndex = static_cast<int>(data[0]) | (static_cast<int>(data[1]) << 8);
      progress.sectionPage = static_cast<int>(data[2]) | (static_cast<int>(data[3]) << 8);
    } else if (type == ContentType::Xtc) {
      progress.flatPage = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                          (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
    } else {
      progress.sectionPage = static_cast<int>(data[0]) | (static_cast<int>(data[1]) << 8);
    }
    file.close();
    return progress;
  }

  uint32_t magic;
  uint8_t version;
  uint8_t storedType;
  int32_t spineIndex;
  uint32_t page;
  const bool readOk = fileSize == kProgressFileSize && serialization::readPodChecked(file, magic) &&
                      serialization::readPodChecked(file, version) && serialization::readPodChecked(file, storedType) &&
                      serialization::readPodChecked(file, spineIndex) && serialization::readPodChecked(file, page);
  file.close();
  if (!readOk || magic != kProgressMagic || version != kProgressVersion || storedType != static_cast<uint8_t>(type) ||
      (type != ContentType::Xtc && page > static_cast<uint32_t>(INT_MAX))) {
    LOG_ERR(TAG, "Invalid progress file, using defaults");
    return progress;
  }

  if (type == ContentType::Epub || type == ContentType::Fb2) {
    progress.spineIndex = static_cast<int>(spineIndex);
    progress.sectionPage = static_cast<int>(page);
  } else if (type == ContentType::Xtc) {
    progress.flatPage = page;
  } else {
    progress.sectionPage = static_cast<int>(page);
  }
  return progress;
}

ProgressManager::Progress ProgressManager::validate(Core& core, ContentType type, const Progress& progress) {
  Progress validated = progress;

  if (type == ContentType::Epub || type == ContentType::Fb2) {
    // Validate spine index (section index for FB2)
    size_t spineCount = 1;
    if (type == ContentType::Epub) {
      auto* provider = core.content.asEpub();
      if (provider && provider->getEpub()) {
        spineCount = provider->getEpub()->getSpineItemsCount();
      }
    } else {  // Fb2
      auto* fb2Provider = core.content.asFb2();
      if (fb2Provider && fb2Provider->getFb2()) {
        spineCount = fb2Provider->getSectionCount();
      }
    }
    if (validated.spineIndex < 0) {
      validated.spineIndex = 0;
    }
    if (validated.spineIndex >= static_cast<int>(spineCount)) {
      // Old FB2 progress stored flat page in first 2 bytes (bytes 2-3 were 0).
      // If spineIndex >= sectionCount, this is likely an old-format file.
      // Reset to start of book as safe fallback.
      if (type == ContentType::Fb2) {
        validated.spineIndex = 0;
        validated.sectionPage = 0;
      } else {
        validated.spineIndex = spineCount > 0 ? spineCount - 1 : 0;
        validated.sectionPage = 0;
      }
    }
  } else if (type == ContentType::Xtc) {
    // Validate flat page
    uint32_t total = core.content.pageCount();
    if (validated.flatPage >= total) {
      validated.flatPage = total > 0 ? total - 1 : 0;
    }
  }
  // TXT/Markdown: page validation happens during cache creation

  return validated;
}

}  // namespace papyrix
