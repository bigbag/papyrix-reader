#include "ExternalFont.h"

#include <Logging.h>

#define TAG "EXT_FONT"

#include <algorithm>
#include <cstring>
#include <vector>

ExternalFont::~ExternalFont() { unload(); }

void ExternalFont::unload() {
  if (_fontFile) {
    _fontFile.close();
  }
  _isLoaded = false;
  _fontName[0] = '\0';
  _fontSize = 0;
  _charWidth = 0;
  _charHeight = 0;
  _bytesPerRow = 0;
  _bytesPerChar = 0;
  _accessCounter = 0;

  // Clear cache and hash table
  for (int i = 0; i < CACHE_SIZE; i++) {
    _cache[i].codepoint = 0xFFFFFFFF;
    _cache[i].lastUsed = 0;
    _cache[i].notFound = false;
    _hashTable[i] = HASH_EMPTY;
  }
}

bool ExternalFont::parseFilename(const char* filepath) {
  // Extract filename from path
  const char* filename = strrchr(filepath, '/');
  if (filename) {
    filename++;  // Skip '/'
  } else {
    filename = filepath;
  }

  // Parse format: FontName_size_WxH.bin
  // Example: KingHwaOldSong_38_33x39.bin

  char nameCopy[64];
  strncpy(nameCopy, filename, sizeof(nameCopy) - 1);
  nameCopy[sizeof(nameCopy) - 1] = '\0';

  // Remove .bin extension
  char* ext = strstr(nameCopy, ".bin");
  if (!ext) {
    LOG_ERR(TAG, "Invalid filename: no .bin extension");
    return false;
  }
  *ext = '\0';

  // Find _WxH part from the end
  char* lastUnderscore = strrchr(nameCopy, '_');
  if (!lastUnderscore) {
    LOG_ERR(TAG, "Invalid filename format");
    return false;
  }

  // Parse WxH
  int w, h;
  if (sscanf(lastUnderscore + 1, "%dx%d", &w, &h) != 2) {
    LOG_ERR(TAG, "Failed to parse dimensions");
    return false;
  }
  _charWidth = (uint8_t)w;
  _charHeight = (uint8_t)h;

  // Validate dimensions
  static constexpr uint8_t MAX_CHAR_DIM = 64;
  if (_charWidth > MAX_CHAR_DIM || _charHeight > MAX_CHAR_DIM) {
    LOG_ERR(TAG, "Dimensions too large: %dx%d (max %d). Using default font.", _charWidth, _charHeight, MAX_CHAR_DIM);
    return false;
  }

  *lastUnderscore = '\0';

  // Find size
  lastUnderscore = strrchr(nameCopy, '_');
  if (!lastUnderscore) {
    LOG_ERR(TAG, "Invalid filename format: no size");
    return false;
  }

  int size;
  if (sscanf(lastUnderscore + 1, "%d", &size) != 1) {
    LOG_ERR(TAG, "Failed to parse size");
    return false;
  }
  _fontSize = (uint8_t)size;
  *lastUnderscore = '\0';

  // Remaining part is font name
  strncpy(_fontName, nameCopy, sizeof(_fontName) - 1);
  _fontName[sizeof(_fontName) - 1] = '\0';

  // Calculate bytes per char
  _bytesPerRow = (_charWidth + 7) / 8;
  _bytesPerChar = _bytesPerRow * _charHeight;

  if (_bytesPerChar > MAX_GLYPH_BYTES) {
    LOG_ERR(TAG, "Glyph too large: %d bytes (max %d)", _bytesPerChar, MAX_GLYPH_BYTES);
    return false;
  }

  LOG_INF(TAG, "Parsed: name=%s, size=%d, %dx%d, %d bytes/char", _fontName, _fontSize, _charWidth, _charHeight,
          _bytesPerChar);

  return true;
}

bool ExternalFont::load(const char* filepath) {
  unload();

  if (!parseFilename(filepath)) {
    return false;
  }

  if (!SdMan.openFileForRead("EXT_FONT", filepath, _fontFile)) {
    LOG_ERR(TAG, "Failed to open: %s", filepath);
    return false;
  }

  // Validate file size
  static constexpr uint32_t MAX_FONT_FILE_SIZE = 32 * 1024 * 1024;  // 32MB max
  uint32_t fileSize = _fontFile.size();
  if (fileSize == 0 || fileSize > MAX_FONT_FILE_SIZE) {
    LOG_ERR(TAG, "Invalid file size: %u bytes (max 32MB). Using default font.", fileSize);
    _fontFile.close();
    return false;
  }

  _isLoaded = true;
  LOG_INF(TAG, "Loaded: %s", filepath);
  return true;
}

int ExternalFont::findInCache(uint32_t codepoint) {
  // O(1) hash table lookup with linear probing for collisions
  int hash = hashCodepoint(codepoint);
  for (int i = 0; i < CACHE_SIZE; i++) {
    int idx = (hash + i) % CACHE_SIZE;
    int16_t cacheIdx = _hashTable[idx];
    if (cacheIdx == HASH_EMPTY) {
      // Empty slot (never used) - entry not in table
      return -1;
    }
    if (cacheIdx == HASH_TOMBSTONE) {
      // Deleted slot - continue probing
      continue;
    }
    if (_cache[cacheIdx].codepoint == codepoint) {
      return cacheIdx;
    }
  }
  return -1;
}

int ExternalFont::getLruSlot() {
  int lruIndex = 0;
  uint32_t minUsed = _cache[0].lastUsed;

  for (int i = 1; i < CACHE_SIZE; i++) {
    // Prefer unused slots
    if (_cache[i].codepoint == 0xFFFFFFFF) {
      return i;
    }
    if (_cache[i].lastUsed < minUsed) {
      minUsed = _cache[i].lastUsed;
      lruIndex = i;
    }
  }
  return lruIndex;
}

bool ExternalFont::readGlyphFromSD(uint32_t codepoint, uint8_t* buffer) {
  if (!_fontFile) {
    return false;
  }

  // Calculate offset
  uint32_t offset = codepoint * _bytesPerChar;

  // Seek and read
  if (!_fontFile.seek(offset)) {
    return false;
  }

  size_t bytesRead = _fontFile.read(buffer, _bytesPerChar);
  if (bytesRead != _bytesPerChar) {
    // May be end of file or other error, fill with zeros
    memset(buffer, 0, _bytesPerChar);
  }

  return true;
}

const uint8_t* ExternalFont::getGlyph(uint32_t codepoint) {
  if (!_isLoaded) {
    return nullptr;
  }

  // First check cache (O(1) with hash table)
  int cacheIndex = findInCache(codepoint);
  if (cacheIndex >= 0) {
    _cache[cacheIndex].lastUsed = ++_accessCounter;
    // Return nullptr if this codepoint was previously marked as not found
    if (_cache[cacheIndex].notFound) {
      return nullptr;
    }
    return _cache[cacheIndex].bitmap;
  }

  // Cache miss, need to read from SD card
  int slot = getLruSlot();

  // If replacing an existing entry, mark it as tombstone in hash table
  if (_cache[slot].codepoint != 0xFFFFFFFF) {
    int oldHash = hashCodepoint(_cache[slot].codepoint);
    for (int i = 0; i < CACHE_SIZE; i++) {
      int idx = (oldHash + i) % CACHE_SIZE;
      if (_hashTable[idx] == slot) {
        _hashTable[idx] = HASH_TOMBSTONE;
        break;
      }
    }
  }

  // Read glyph from SD card
  bool readSuccess = readGlyphFromSD(codepoint, _cache[slot].bitmap);

  // Calculate metrics and check if glyph is empty
  uint8_t minX = _charWidth;
  uint8_t maxX = 0;
  bool isEmpty = true;

  if (readSuccess && _bytesPerChar > 0) {
    for (int y = 0; y < _charHeight; y++) {
      for (int x = 0; x < _charWidth; x++) {
        int byteIndex = y * _bytesPerRow + (x / 8);
        int bitIndex = 7 - (x % 8);
        if ((_cache[slot].bitmap[byteIndex] >> bitIndex) & 1) {
          isEmpty = false;
          if (x < minX) minX = x;
          if (x > maxX) maxX = x;
        }
      }
    }
  }

  // Update cache entry
  _cache[slot].codepoint = codepoint;
  _cache[slot].lastUsed = ++_accessCounter;

  // Check if this is a whitespace character (U+2000-U+200F: various spaces, U+3000: ideographic space)
  bool isWhitespace = (codepoint >= 0x2000 && codepoint <= 0x200F) || codepoint == 0x3000;

  // Mark as notFound only if read failed or (empty AND not whitespace AND non-ASCII)
  // Whitespace characters are expected to be empty but should still be rendered
  _cache[slot].notFound = !readSuccess || (isEmpty && !isWhitespace && codepoint > 0x7F);

  // Store metrics
  if (!isEmpty) {
    _cache[slot].minX = minX;
    // Variable width: content width + 2px padding
    _cache[slot].advanceX = (maxX - minX + 1) + 2;
  } else {
    _cache[slot].minX = 0;
    // Special handling for whitespace characters
    if (isWhitespace) {
      // em-space (U+2003) and similar should be full-width (same as CJK char)
      // en-space (U+2002) should be half-width
      // Other spaces use appropriate widths
      if (codepoint == 0x2003) {
        // em-space: full CJK character width
        _cache[slot].advanceX = _charWidth;
      } else if (codepoint == 0x2002) {
        // en-space: half CJK character width
        _cache[slot].advanceX = _charWidth / 2;
      } else if (codepoint == 0x3000) {
        // Ideographic space (CJK full-width space): full width
        _cache[slot].advanceX = _charWidth;
      } else {
        // Other spaces: use standard space width
        _cache[slot].advanceX = _charWidth / 3;
      }
    } else {
      // Fallback for other empty glyphs
      _cache[slot].advanceX = _charWidth / 3;
    }
  }

  // Add to hash table (reuse tombstones or empty slots)
  int hash = hashCodepoint(codepoint);
  for (int i = 0; i < CACHE_SIZE; i++) {
    int idx = (hash + i) % CACHE_SIZE;
    if (_hashTable[idx] == HASH_EMPTY || _hashTable[idx] == HASH_TOMBSTONE) {
      _hashTable[idx] = slot;
      break;
    }
  }

  if (_cache[slot].notFound) {
    return nullptr;
  }

  return _cache[slot].bitmap;
}

bool ExternalFont::getGlyphMetrics(uint32_t codepoint, uint8_t* outMinX, uint8_t* outAdvanceX) {
  int idx = findInCache(codepoint);
  if (idx >= 0 && !_cache[idx].notFound) {
    if (outMinX) *outMinX = _cache[idx].minX;
    if (outAdvanceX) *outAdvanceX = _cache[idx].advanceX;
    return true;
  }
  return false;
}

void ExternalFont::preloadGlyphs(const uint32_t* codepoints, size_t count) {
  if (!_isLoaded || !codepoints || count == 0) {
    return;
  }

  // Limit to cache size to avoid thrashing
  const size_t maxLoad = std::min(count, static_cast<size_t>(CACHE_SIZE));

  // Create a sorted copy for sequential SD card access
  // Sequential reads are much faster than random seeks
  std::vector<uint32_t> sorted(codepoints, codepoints + maxLoad);
  std::sort(sorted.begin(), sorted.end());

  // Remove duplicates
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

  LOG_INF(TAG, "Preloading %zu unique glyphs", sorted.size());
  const unsigned long startTime = millis();

  size_t loaded = 0;
  size_t skipped = 0;

  for (uint32_t cp : sorted) {
    // Skip if already in cache
    if (findInCache(cp) >= 0) {
      skipped++;
      continue;
    }

    // Load into cache (getGlyph handles all the cache management)
    getGlyph(cp);
    loaded++;
  }

  LOG_INF(TAG, "Preload done: %zu loaded, %zu already cached, took %lums", loaded, skipped, millis() - startTime);
}

void ExternalFont::logCacheStats() const {
  int used = 0;
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (_cache[i].codepoint != 0xFFFFFFFF) used++;
  }
  LOG_DBG(TAG, "Cache: %d/%d slots used (~%dKB)", used, CACHE_SIZE, (used * sizeof(CacheEntry)) / 1024);
}
