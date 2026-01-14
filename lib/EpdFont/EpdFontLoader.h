#pragma once

#include <cstdint>

#include "EpdFontData.h"

/**
 * Loader for binary .epdfont files from SD card.
 *
 * Binary file format (.epdfont):
 *   Header (16 bytes):
 *     - Magic: "EPDF" (4 bytes)
 *     - Version: uint16_t (2 bytes)
 *     - Flags: uint16_t (2 bytes, bit 0 = is2Bit)
 *     - Reserved: 8 bytes
 *
 *   Metrics (12 bytes):
 *     - advanceY: uint8_t
 *     - ascender: int16_t
 *     - descender: int16_t
 *     - intervalCount: uint32_t
 *     - glyphCount: uint32_t (inferred from intervals)
 *     - bitmapSize: uint32_t
 *
 *   Intervals: intervalCount * sizeof(EpdUnicodeInterval)
 *   Glyphs: glyphCount * sizeof(EpdGlyph)
 *   Bitmap: bitmapSize bytes
 */
class EpdFontLoader {
 public:
  static constexpr uint32_t MAGIC = 0x46445045;  // "EPDF" in little-endian
  static constexpr uint16_t VERSION = 1;

  struct LoadResult {
    bool success;
    EpdFontData* fontData;
    uint8_t* bitmap;
    EpdGlyph* glyphs;
    EpdUnicodeInterval* intervals;
  };

  /**
   * Load a font from a binary .epdfont file.
   * Allocates memory for font data, which caller must free.
   *
   * @param path Path to .epdfont file
   * @return LoadResult with success status and allocated data
   */
  static LoadResult loadFromFile(const char* path);

  /**
   * Free memory allocated by loadFromFile.
   */
  static void freeLoadResult(LoadResult& result);

 private:
  struct FileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint8_t reserved[8];
  } __attribute__((packed));

  struct FileMetrics {
    uint8_t advanceY;
    uint8_t padding;
    int16_t ascender;
    int16_t descender;
    uint32_t intervalCount;
    uint32_t glyphCount;
    uint32_t bitmapSize;
  } __attribute__((packed));
};
