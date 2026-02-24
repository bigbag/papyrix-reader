#include "ProgressManager.h"

#include <Arduino.h>
#include <Logging.h>
#include <SdFat.h>

#include <cstdio>

#include "../content/ContentHandle.h"
#include "../core/Core.h"

#define TAG "PROGRESS"

namespace papyrix {

bool ProgressManager::save(Core& core, const char* cacheDir, ContentType type, const Progress& progress) {
  if (!cacheDir || cacheDir[0] == '\0') {
    return false;
  }

  char progressPath[280];
  snprintf(progressPath, sizeof(progressPath), "%s/progress.bin", cacheDir);

  FsFile file;
  auto result = core.storage.openWrite(progressPath, file);
  if (!result.ok()) {
    LOG_ERR(TAG, "Failed to save progress to %s", progressPath);
    return false;
  }

  uint8_t data[4];

  if (type == ContentType::Epub) {
    // EPUB: save spine index and section page (4 bytes)
    data[0] = progress.spineIndex & 0xFF;
    data[1] = (progress.spineIndex >> 8) & 0xFF;
    data[2] = progress.sectionPage & 0xFF;
    data[3] = (progress.sectionPage >> 8) & 0xFF;
    file.write(data, 4);
    LOG_DBG(TAG, "Saved EPUB: spine=%d page=%d", progress.spineIndex, progress.sectionPage);
  } else if (type == ContentType::Xtc) {
    // XTC: save flat page number (4 bytes)
    data[0] = progress.flatPage & 0xFF;
    data[1] = (progress.flatPage >> 8) & 0xFF;
    data[2] = (progress.flatPage >> 16) & 0xFF;
    data[3] = (progress.flatPage >> 24) & 0xFF;
    file.write(data, 4);
    LOG_DBG(TAG, "Saved XTC: page %u", progress.flatPage);
  } else {
    // TXT/Markdown: save section page (4 bytes)
    data[0] = progress.sectionPage & 0xFF;
    data[1] = (progress.sectionPage >> 8) & 0xFF;
    data[2] = 0;
    data[3] = 0;
    file.write(data, 4);
    LOG_DBG(TAG, "Saved text: page %d", progress.sectionPage);
  }

  file.close();
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

  // Validate file size before reading
  if (file.size() < 4) {
    LOG_ERR(TAG, "Corrupted file (too small), using defaults");
    file.close();
    return progress;
  }

  uint8_t data[4];
  if (file.read(data, 4) != 4) {
    LOG_ERR(TAG, "Read failed, using defaults");
    file.close();
    return progress;
  }

  if (type == ContentType::Epub) {
    progress.spineIndex = data[0] | (data[1] << 8);
    progress.sectionPage = data[2] | (data[3] << 8);
    LOG_DBG(TAG, "Loaded EPUB: spine=%d page=%d", progress.spineIndex, progress.sectionPage);
  } else if (type == ContentType::Xtc) {
    progress.flatPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    LOG_DBG(TAG, "Loaded XTC: page %u", progress.flatPage);
  } else {
    // TXT/Markdown
    progress.sectionPage = data[0] | (data[1] << 8);
    LOG_DBG(TAG, "Loaded text: page %d", progress.sectionPage);
  }

  file.close();
  return progress;
}

ProgressManager::Progress ProgressManager::validate(Core& core, ContentType type, const Progress& progress) {
  Progress validated = progress;

  if (type == ContentType::Epub) {
    // Validate spine index
    auto* provider = core.content.asEpub();
    if (provider && provider->getEpub()) {
      uint32_t spineCount = provider->getEpub()->getSpineItemsCount();
      if (validated.spineIndex < 0) {
        validated.spineIndex = 0;
      }
      if (validated.spineIndex >= static_cast<int>(spineCount)) {
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
