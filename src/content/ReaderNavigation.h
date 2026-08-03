#pragma once

#include <cstdint>

#include "ContentTypes.h"

class PageCache;

namespace papyrix {

// ReaderNavigation - Handles page traversal logic for different content types
// Separates navigation logic from state management for better testability
class ReaderNavigation {
 public:
  // Reading position
  struct Position {
    int spineIndex = 0;     // EPUB: chapter index in spine
    int sectionPage = 0;    // Page within current section (EPUB, TXT, Markdown)
    uint32_t flatPage = 0;  // XTC: absolute page number
  };

  // Navigation result
  struct NavResult {
    Position position;
    bool needsRender = false;
    bool needsCacheReset = false;  // Cache needs to be rebuilt (e.g., chapter change)
  };

  // Navigate forward one page
  // For EPUB: advances within section or to next chapter
  // For XTC: increments flat page number
  // For TXT/Markdown: advances section page
  static NavResult next(ContentType type, const Position& current, const PageCache* cache, uint32_t totalPages);

  // Navigate backward one page
  // For EPUB: goes back within section or to previous chapter
  // For XTC: decrements flat page number
  // For TXT/Markdown: decrements section page
  static NavResult prev(ContentType type, const Position& current, const PageCache* cache);

  // Move past a complete 0-page EPUB/FB2 section. A result with needsRender=false
  // means there is no adjacent section in the requested direction.
  static NavResult skipEmptySection(const Position& current, int spineCount, bool preferBack);

  // Check if cache needs extension for the given page
  static bool needsCacheExtension(const PageCache* cache, int sectionPage);
};

}  // namespace papyrix
