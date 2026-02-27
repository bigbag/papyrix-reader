#include "BookmarkManager.h"

#include <Arduino.h>
#include <Logging.h>
#include <SdFat.h>

#include <cstdio>
#include <cstring>

#include "../core/Core.h"

#define TAG "BOOKMARK"

namespace papyrix {

bool BookmarkManager::save(Core& core, const char* cacheDir, ContentType type, const Bookmark* bookmarks, int count) {
  if (!cacheDir || cacheDir[0] == '\0') return false;
  if (count < 0 || count > MAX_BOOKMARKS) return false;

  char path[280];
  snprintf(path, sizeof(path), "%s/bookmarks.bin", cacheDir);

  FsFile file;
  auto result = core.storage.openWrite(path, file);
  if (!result.ok()) {
    LOG_ERR(TAG, "Failed to save bookmarks to %s", path);
    return false;
  }

  uint8_t cnt = static_cast<uint8_t>(count);
  file.write(&cnt, 1);
  if (count > 0) {
    file.write(reinterpret_cast<const uint8_t*>(bookmarks), static_cast<size_t>(count) * sizeof(Bookmark));
  }
  file.close();
  LOG_DBG(TAG, "Saved %d bookmarks", count);

  snprintf(path, sizeof(path), "%s/bookmarks.txt", cacheDir);
  result = core.storage.openWrite(path, file);
  if (!result.ok()) {
    LOG_ERR(TAG, "Failed to export bookmarks.txt");
    return true;
  }

  char line[128];
  for (int i = 0; i < count; i++) {
    const Bookmark& b = bookmarks[i];
    int len = 0;
    if (type == ContentType::Epub) {
      len = snprintf(line, sizeof(line), "Ch %d, Page %d: %s\n", b.spineIndex + 1, b.sectionPage + 1, b.label);
    } else if (type == ContentType::Xtc) {
      len = snprintf(line, sizeof(line), "Page %u: %s\n", static_cast<unsigned>(b.flatPage + 1), b.label);
    } else {
      len = snprintf(line, sizeof(line), "Page %d: %s\n", b.sectionPage + 1, b.label);
    }
    if (len > 0) {
      file.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(len));
    }
  }
  file.close();

  return true;
}

int BookmarkManager::load(Core& core, const char* cacheDir, Bookmark* bookmarks, int maxCount) {
  if (!cacheDir || cacheDir[0] == '\0') return 0;

  char path[280];
  snprintf(path, sizeof(path), "%s/bookmarks.bin", cacheDir);

  FsFile file;
  auto result = core.storage.openRead(path, file);
  if (!result.ok()) {
    LOG_DBG(TAG, "No saved bookmarks found");
    return 0;
  }

  if (file.size() < 1) {
    LOG_ERR(TAG, "Corrupted bookmarks file (too small)");
    file.close();
    return 0;
  }

  uint8_t cnt;
  if (file.read(&cnt, 1) != 1) {
    LOG_ERR(TAG, "Failed to read bookmark count");
    file.close();
    return 0;
  }

  int toLoad = cnt;
  if (toLoad > maxCount) toLoad = maxCount;
  if (toLoad > MAX_BOOKMARKS) toLoad = MAX_BOOKMARKS;

  if (toLoad > 0) {
    size_t expected = static_cast<size_t>(toLoad) * sizeof(Bookmark);
    if (file.size() - 1 < static_cast<int64_t>(expected)) {
      LOG_ERR(TAG, "Corrupted bookmarks file (truncated)");
      file.close();
      return 0;
    }
    int bytesRead = file.read(reinterpret_cast<uint8_t*>(bookmarks), expected);
    if (bytesRead != static_cast<int>(expected)) {
      LOG_ERR(TAG, "Failed to read bookmarks data");
      file.close();
      return 0;
    }
  }

  file.close();
  LOG_DBG(TAG, "Loaded %d bookmarks", toLoad);
  return toLoad;
}

int BookmarkManager::findAt(const Bookmark* bookmarks, int count, ContentType type, int spineIndex, int sectionPage,
                            uint32_t flatPage) {
  for (int i = 0; i < count; i++) {
    if (type == ContentType::Epub) {
      if (bookmarks[i].spineIndex == spineIndex && bookmarks[i].sectionPage == sectionPage) {
        return i;
      }
    } else if (type == ContentType::Xtc) {
      if (bookmarks[i].flatPage == flatPage) {
        return i;
      }
    } else {
      if (bookmarks[i].sectionPage == sectionPage) {
        return i;
      }
    }
  }
  return -1;
}

}  // namespace papyrix
