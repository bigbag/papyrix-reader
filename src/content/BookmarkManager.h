#pragma once

#include <cstdint>

#include "ContentTypes.h"

namespace papyrix {

struct Core;

struct Bookmark {
  int16_t spineIndex;
  int16_t sectionPage;
  uint32_t flatPage;
  char label[64];
};

class BookmarkManager {
 public:
  static constexpr int MAX_BOOKMARKS = 20;

  static bool save(Core& core, const char* cacheDir, ContentType type, const Bookmark* bookmarks, int count);
  static int load(Core& core, const char* cacheDir, Bookmark* bookmarks, int maxCount);
  static int findAt(const Bookmark* bookmarks, int count, ContentType type, int spineIndex, int sectionPage,
                    uint32_t flatPage);
};

}  // namespace papyrix
