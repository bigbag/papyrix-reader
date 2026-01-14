#include "CacheManager.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <cstring>
#include <vector>

#include "config.h"

namespace CacheManager {

int clearAllBookCaches() {
  if (!SdMan.ready()) {
    Serial.printf("[%lu] [CACHE] SD card not ready\n", millis());
    return -1;
  }

  auto dir = SdMan.open(PAPYRIX_DIR);
  if (!dir) {
    Serial.printf("[%lu] [CACHE] Failed to open cache directory\n", millis());
    return -1;
  }
  if (!dir.isDirectory()) {
    Serial.printf("[%lu] [CACHE] Cache path is not a directory\n", millis());
    dir.close();
    return -1;
  }

  // First pass: collect all book cache paths
  // (Don't delete while iterating to avoid skipping entries)
  std::vector<std::string> pathsToDelete;
  char name[128];

  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));

    // Check if this is a book cache directory
    const bool isBookCache =
        (strncmp(name, "epub_", 5) == 0) || (strncmp(name, "txt_", 4) == 0) || (strncmp(name, "xtc_", 4) == 0);

    entry.close();

    if (isBookCache) {
      std::string fullPath = PAPYRIX_DIR;
      fullPath += "/";
      fullPath += name;
      pathsToDelete.push_back(fullPath);
    }
  }

  dir.close();

  // Second pass: delete collected paths
  int deletedCount = 0;
  for (const auto& path : pathsToDelete) {
    Serial.printf("[%lu] [CACHE] Deleting cache: %s\n", millis(), path.c_str());

    if (SdMan.removeDir(path.c_str())) {
      deletedCount++;
    } else {
      Serial.printf("[%lu] [CACHE] Failed to delete: %s\n", millis(), path.c_str());
    }
  }

  Serial.printf("[%lu] [CACHE] Deleted %d book cache(s)\n", millis(), deletedCount);
  return deletedCount;
}

}  // namespace CacheManager
