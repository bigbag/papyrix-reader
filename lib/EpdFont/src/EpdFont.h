#pragma once
#include "EpdFontData.h"

// Direct-mapped glyph cache for O(1) lookup of hot glyphs
// 64 entries * 8 bytes = 512 bytes per font
class GlyphCache {
 public:
  static constexpr int CACHE_SIZE = 64;

  GlyphCache() { clear(); }

  void clear() {
    for (int i = 0; i < CACHE_SIZE; i++) {
      entries[i].codepoint = 0xFFFFFFFF;  // Invalid sentinel
      entries[i].glyph = nullptr;
    }
  }

  const EpdGlyph* lookup(uint32_t cp) const {
    const int idx = cp % CACHE_SIZE;
    if (entries[idx].codepoint == cp) {
      return entries[idx].glyph;
    }
    return nullptr;
  }

  void store(uint32_t cp, const EpdGlyph* glyph) {
    const int idx = cp % CACHE_SIZE;
    entries[idx].codepoint = cp;
    entries[idx].glyph = glyph;
  }

 private:
  struct CacheEntry {
    uint32_t codepoint;
    const EpdGlyph* glyph;
  };
  CacheEntry entries[CACHE_SIZE];
};

class EpdFont {
  void getTextBounds(const char* string, int startX, int startY, int* minX, int* minY, int* maxX, int* maxY) const;

 public:
  const EpdFontData* data;
  explicit EpdFont(const EpdFontData* data) : data(data) {}
  ~EpdFont() = default;
  void getTextDimensions(const char* string, int* w, int* h) const;
  const EpdGlyph* getGlyph(uint32_t cp) const;

 private:
  mutable GlyphCache glyphCache;
};
