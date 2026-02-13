#pragma once

#include <SDCardManager.h>

#include <cstdint>

#include "EpdFontData.h"

/**
 * Streaming font loader for .epdfont files.
 *
 * Unlike EpdFont which loads the entire bitmap into RAM (~50-100KB),
 * StreamingEpdFont keeps the file open and streams glyph bitmaps on demand
 * with an LRU cache (~10-25KB total).
 *
 * Memory comparison for typical 50KB font:
 *   - EpdFont: ~70KB (intervals + glyphs + bitmap all in RAM)
 *   - StreamingEpdFont: ~25KB (intervals + glyphs + cache only)
 *
 * Trade-off: Slightly slower glyph access (SD card reads on cache miss)
 *            but significantly lower RAM usage.
 */
class StreamingEpdFont {
 public:
  StreamingEpdFont();
  ~StreamingEpdFont();

  StreamingEpdFont(const StreamingEpdFont&) = delete;
  StreamingEpdFont& operator=(const StreamingEpdFont&) = delete;

  /**
   * Load font from .epdfont file (streaming mode).
   * Loads intervals and glyph table into RAM, keeps file open for bitmap streaming.
   *
   * @param path Path to .epdfont file on SD card
   * @return true on success
   */
  bool load(const char* path);

  /**
   * Unload font and free all resources.
   */
  void unload();

  /**
   * Check if font is loaded and ready.
   */
  bool isLoaded() const { return _isLoaded; }

  /**
   * Get glyph data for a unicode codepoint.
   * Uses LRU cache - may read from SD card on cache miss.
   *
   * @param cp Unicode codepoint
   * @return Pointer to glyph data, or nullptr if not found
   */
  const EpdGlyph* getGlyph(uint32_t cp);

  /**
   * Get glyph bitmap data for a glyph.
   * This is the actual bitmap pixels, streamed from SD or cache.
   *
   * @param glyph Pointer to glyph obtained from this font's getGlyph() method.
   *              Must not be a glyph pointer from a different font instance.
   * @return Pointer to bitmap data, or nullptr on error
   */
  const uint8_t* getGlyphBitmap(const EpdGlyph* glyph);

  /**
   * Calculate text dimensions without rendering.
   */
  void getTextDimensions(const char* string, int* w, int* h) const;

  /**
   * Check if string contains any printable characters.
   */
  bool hasPrintableChars(const char* string) const;

  /**
   * Get font data structure (for compatibility with EpdFont interface).
   * Note: The bitmap pointer is nullptr in streaming mode.
   */
  const EpdFontData* getData() const { return &_fontData; }

  // Font metrics accessors
  uint8_t getAdvanceY() const { return _fontData.advanceY; }
  int getAscender() const { return _fontData.ascender; }
  int getDescender() const { return _fontData.descender; }
  bool is2Bit() const { return _fontData.is2Bit; }

  /**
   * Get total RAM usage of this font instance.
   * Includes intervals, glyphs, and cache.
   */
  size_t getMemoryUsage() const;

  /**
   * Log cache statistics for debugging.
   */
  void logCacheStats() const;

  /**
   * Get the configured cache size.
   */
  static constexpr int getCacheSize() { return CACHE_SIZE; }

 private:
  // Cache configuration
  // 64 entries balances hit rate vs RAM for Latin/Western fonts (fewer unique glyphs per page than CJK)
  static constexpr int CACHE_SIZE = 64;
  static constexpr uint32_t INVALID_CODEPOINT = 0xFFFFFFFF;

  // Maximum allowed glyph bitmap size (defense against corrupted font files)
  // Typical glyph is ~200 bytes; 4KB allows for large CJK characters
  static constexpr uint16_t MAX_GLYPH_BITMAP_SIZE = 4096;

  // Hash table markers
  static constexpr int16_t HASH_EMPTY = -1;
  static constexpr int16_t HASH_TOMBSTONE = -2;

  // Rehash when tombstones exceed 25% of table size to maintain O(1) lookup
  static constexpr int TOMBSTONE_REHASH_THRESHOLD = CACHE_SIZE / 4;

  // Font metadata (in RAM)
  EpdFontData _fontData;
  EpdGlyph* _glyphs = nullptr;
  EpdUnicodeInterval* _intervals = nullptr;
  uint32_t _glyphCount = 0;

  // File handle (kept open for streaming)
  FsFile _fontFile;
  uint32_t _bitmapOffset = 0;  // File offset where bitmap data starts
  bool _isLoaded = false;

  // Memory tracking
  size_t _glyphsSize = 0;
  size_t _intervalsSize = 0;

  // LRU bitmap cache
  struct CachedBitmap {
    uint32_t glyphIndex = INVALID_CODEPOINT;  // Which glyph this bitmap belongs to
    uint8_t* bitmap = nullptr;                // Dynamically allocated per glyph size
    uint16_t bitmapSize = 0;                  // Size of bitmap allocation
    uint32_t lastUsed = 0;                    // For LRU eviction
  };
  CachedBitmap _cache[CACHE_SIZE];
  int16_t _hashTable[CACHE_SIZE];
  uint32_t _accessCounter = 0;
  size_t _totalCacheAllocation = 0;  // Track total bytes allocated in cache
  int _tombstoneCount = 0;           // Track tombstones to trigger rehashing

  // Cache statistics
  mutable uint32_t _cacheHits = 0;
  mutable uint32_t _cacheMisses = 0;

  // Glyph lookup cache (codepoint -> glyph pointer, for O(1) repeated lookups)
  static constexpr int GLYPH_CACHE_SIZE = 64;
  struct GlyphCacheEntry {
    uint32_t codepoint = INVALID_CODEPOINT;
    const EpdGlyph* glyph = nullptr;
  };
  mutable GlyphCacheEntry _glyphCache[GLYPH_CACHE_SIZE];

  // Helper methods
  static int hashIndex(uint32_t index) { return index % CACHE_SIZE; }
  int findInBitmapCache(uint32_t glyphIndex);
  int getLruSlot();
  bool loadGlyphBitmap(uint32_t glyphIndex, CachedBitmap& entry);
  const EpdGlyph* lookupGlyph(uint32_t cp) const;
  void rehashTable();  // Rebuild hash table to clear tombstones
};
