#include "StreamingEpdFont.h"

#include <Utf8.h>

#include <algorithm>
#include <cstring>

#include "EpdFontLoader.h"

StreamingEpdFont::StreamingEpdFont() {
  memset(&_fontData, 0, sizeof(_fontData));
  for (int i = 0; i < CACHE_SIZE; i++) {
    _hashTable[i] = HASH_EMPTY;
  }
}

StreamingEpdFont::~StreamingEpdFont() { unload(); }

bool StreamingEpdFont::load(const char* path) {
  unload();

  // Use EpdFontLoader to parse the file
  auto result = EpdFontLoader::loadForStreaming(path);
  if (!result.success) {
    return false;
  }

  // Transfer ownership of allocated data
  _glyphs = result.glyphs;
  _intervals = result.intervals;
  _glyphCount = result.glyphCount;
  _glyphsSize = result.glyphsSize;
  _intervalsSize = result.intervalsSize;
  _bitmapOffset = result.bitmapOffset;

  // Copy font metadata
  _fontData.bitmap = nullptr;  // No bitmap in RAM - we stream it
  _fontData.glyph = _glyphs;
  _fontData.intervals = _intervals;
  _fontData.intervalCount = result.fontData.intervalCount;
  _fontData.advanceY = result.fontData.advanceY;
  _fontData.ascender = result.fontData.ascender;
  _fontData.descender = result.fontData.descender;
  _fontData.is2Bit = result.fontData.is2Bit;

  // Open file and keep it open for streaming
  if (!SdMan.openFileForRead("SFONT", path, _fontFile)) {
    delete[] _glyphs;
    delete[] _intervals;
    _glyphs = nullptr;
    _intervals = nullptr;
    return false;
  }

  _isLoaded = true;
  return true;
}

void StreamingEpdFont::unload() {
  if (_fontFile) {
    _fontFile.close();
  }

  delete[] _glyphs;
  delete[] _intervals;

  // Free all cached bitmaps
  for (int i = 0; i < CACHE_SIZE; i++) {
    delete[] _cache[i].bitmap;
    _cache[i].bitmap = nullptr;
    _cache[i].glyphIndex = INVALID_CODEPOINT;
    _cache[i].bitmapSize = 0;
    _cache[i].lastUsed = 0;
    _hashTable[i] = HASH_EMPTY;
  }

  // Clear glyph lookup cache
  for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
    _glyphCache[i].codepoint = INVALID_CODEPOINT;
    _glyphCache[i].glyph = nullptr;
  }

  _glyphs = nullptr;
  _intervals = nullptr;
  _glyphCount = 0;
  _glyphsSize = 0;
  _intervalsSize = 0;
  _bitmapOffset = 0;
  _isLoaded = false;
  _accessCounter = 0;
  _totalCacheAllocation = 0;
  _tombstoneCount = 0;
  _cacheHits = 0;
  _cacheMisses = 0;

  memset(&_fontData, 0, sizeof(_fontData));
}

const EpdGlyph* StreamingEpdFont::lookupGlyph(uint32_t cp) const {
  // Check glyph cache first (O(1) for hot glyphs)
  const int cacheIdx = cp % GLYPH_CACHE_SIZE;
  if (_glyphCache[cacheIdx].codepoint == cp) {
    return _glyphCache[cacheIdx].glyph;
  }

  // Binary search in intervals (O(log n))
  const int count = _fontData.intervalCount;
  if (count == 0) return nullptr;

  int left = 0;
  int right = count - 1;

  while (left <= right) {
    const int mid = left + (right - left) / 2;
    const EpdUnicodeInterval* interval = &_intervals[mid];

    if (cp < interval->first) {
      right = mid - 1;
    } else if (cp > interval->last) {
      left = mid + 1;
    } else {
      // Found: cp >= interval->first && cp <= interval->last
      const uint32_t glyphIdx = interval->offset + (cp - interval->first);
      if (glyphIdx >= _glyphCount) {
        return nullptr;  // Corrupted font data - index out of bounds
      }
      const EpdGlyph* glyph = &_glyphs[glyphIdx];
      // Store in cache
      _glyphCache[cacheIdx].codepoint = cp;
      _glyphCache[cacheIdx].glyph = glyph;
      return glyph;
    }
  }

  return nullptr;
}

const EpdGlyph* StreamingEpdFont::getGlyph(uint32_t cp) {
  if (!_isLoaded) return nullptr;
  return lookupGlyph(cp);
}

int StreamingEpdFont::findInBitmapCache(uint32_t glyphIndex) {
  // O(1) hash table lookup with linear probing
  int hash = hashIndex(glyphIndex);
  for (int i = 0; i < CACHE_SIZE; i++) {
    int idx = (hash + i) % CACHE_SIZE;
    int16_t cacheIdx = _hashTable[idx];
    if (cacheIdx == HASH_EMPTY) {
      return -1;
    }
    if (cacheIdx == HASH_TOMBSTONE) {
      continue;
    }
    if (_cache[cacheIdx].glyphIndex == glyphIndex) {
      return cacheIdx;
    }
  }
  return -1;
}

int StreamingEpdFont::getLruSlot() {
  int lruIndex = 0;
  uint32_t minUsed = _cache[0].lastUsed;

  for (int i = 1; i < CACHE_SIZE; i++) {
    // Prefer unused slots
    if (_cache[i].glyphIndex == INVALID_CODEPOINT) {
      return i;
    }
    if (_cache[i].lastUsed < minUsed) {
      minUsed = _cache[i].lastUsed;
      lruIndex = i;
    }
  }
  return lruIndex;
}

bool StreamingEpdFont::loadGlyphBitmap(uint32_t glyphIndex, CachedBitmap& entry) {
  if (!_fontFile || glyphIndex >= _glyphCount) {
    return false;
  }

  const EpdGlyph& glyph = _glyphs[glyphIndex];
  const uint16_t dataLen = glyph.dataLength;

  // Validate dataLength against maximum (defense against corrupted font files)
  if (dataLen > MAX_GLYPH_BITMAP_SIZE) {
    return false;
  }

  // Reallocate bitmap buffer if needed
  if (entry.bitmapSize < dataLen) {
    const uint16_t oldSize = entry.bitmapSize;
    delete[] entry.bitmap;
    entry.bitmap = new (std::nothrow) uint8_t[dataLen];
    if (!entry.bitmap) {
      entry.bitmapSize = 0;
      _totalCacheAllocation -= oldSize;
      return false;
    }
    // Update allocation tracking (subtract old, add new)
    _totalCacheAllocation = _totalCacheAllocation - oldSize + dataLen;
    entry.bitmapSize = dataLen;
  }

  // Calculate file position: bitmapOffset + glyph's dataOffset
  // Retry seek+read on transient SD card failures (file handle stays valid)
  uint32_t filePos = _bitmapOffset + glyph.dataOffset;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) delay(50);
    if (!_fontFile.seek(filePos)) continue;
    if (_fontFile.read(entry.bitmap, dataLen) == dataLen) return true;
  }

  return false;
}

const uint8_t* StreamingEpdFont::getGlyphBitmap(const EpdGlyph* glyph) {
  if (!_isLoaded || !glyph) return nullptr;

  // Validate glyph pointer belongs to this font instance (defense against wrong font)
  if (glyph < _glyphs || glyph >= _glyphs + _glyphCount) {
    return nullptr;
  }

  // Calculate glyph index from pointer arithmetic (now safe after validation)
  uint32_t glyphIndex = glyph - _glyphs;

  // Check bitmap cache
  int cacheIndex = findInBitmapCache(glyphIndex);
  if (cacheIndex >= 0) {
    _cache[cacheIndex].lastUsed = ++_accessCounter;
    _cacheHits++;
    return _cache[cacheIndex].bitmap;
  }

  _cacheMisses++;

  // Cache miss - need to load from SD
  int slot = getLruSlot();

  // If replacing an existing entry, mark it as tombstone in hash table
  if (_cache[slot].glyphIndex != INVALID_CODEPOINT) {
    int oldHash = hashIndex(_cache[slot].glyphIndex);
    for (int i = 0; i < CACHE_SIZE; i++) {
      int idx = (oldHash + i) % CACHE_SIZE;
      if (_hashTable[idx] == slot) {
        _hashTable[idx] = HASH_TOMBSTONE;
        _tombstoneCount++;
        break;
      }
    }
    // Update allocation tracking when evicting
    _totalCacheAllocation -= _cache[slot].bitmapSize;

    // Rehash if too many tombstones have accumulated
    if (_tombstoneCount >= TOMBSTONE_REHASH_THRESHOLD) {
      rehashTable();
    }
  }

  // Load glyph bitmap from SD
  if (!loadGlyphBitmap(glyphIndex, _cache[slot])) {
    return nullptr;
  }

  _cache[slot].glyphIndex = glyphIndex;
  _cache[slot].lastUsed = ++_accessCounter;

  // Add to hash table
  int hash = hashIndex(glyphIndex);
  bool inserted = false;
  for (int i = 0; i < CACHE_SIZE; i++) {
    int idx = (hash + i) % CACHE_SIZE;
    if (_hashTable[idx] == HASH_EMPTY || _hashTable[idx] == HASH_TOMBSTONE) {
      _hashTable[idx] = slot;
      inserted = true;
      break;
    }
  }

  // If hash table is full (should not happen with proper LRU eviction), force rehash
  if (!inserted) {
    rehashTable();
    // Re-insert after rehash
    hash = hashIndex(glyphIndex);
    for (int i = 0; i < CACHE_SIZE; i++) {
      int idx = (hash + i) % CACHE_SIZE;
      if (_hashTable[idx] == HASH_EMPTY) {
        _hashTable[idx] = slot;
        break;
      }
    }
  }

  return _cache[slot].bitmap;
}

void StreamingEpdFont::rehashTable() {
  // Clear the hash table
  for (int i = 0; i < CACHE_SIZE; i++) {
    _hashTable[i] = HASH_EMPTY;
  }
  _tombstoneCount = 0;

  // Re-insert all valid cache entries
  for (int slot = 0; slot < CACHE_SIZE; slot++) {
    if (_cache[slot].glyphIndex != INVALID_CODEPOINT) {
      int hash = hashIndex(_cache[slot].glyphIndex);
      for (int i = 0; i < CACHE_SIZE; i++) {
        int idx = (hash + i) % CACHE_SIZE;
        if (_hashTable[idx] == HASH_EMPTY) {
          _hashTable[idx] = slot;
          break;
        }
      }
    }
  }
}

void StreamingEpdFont::getTextDimensions(const char* string, int* w, int* h) const {
  int minX = 0, minY = 0, maxX = 0, maxY = 0;
  int cursorX = 0;
  const int cursorY = 0;

  if (!string || *string == '\0') {
    *w = 0;
    *h = 0;
    return;
  }

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&string)))) {
    const EpdGlyph* glyph = lookupGlyph(cp);
    if (!glyph) {
      glyph = lookupGlyph('?');
    }
    if (!glyph) {
      continue;
    }

    minX = std::min(minX, cursorX + glyph->left);
    maxX = std::max(maxX, cursorX + glyph->left + glyph->width);
    minY = std::min(minY, cursorY + glyph->top - glyph->height);
    maxY = std::max(maxY, cursorY + glyph->top);
    cursorX += glyph->advanceX;
  }

  *w = maxX - minX;
  *h = maxY - minY;
}

bool StreamingEpdFont::hasPrintableChars(const char* string) const {
  int w = 0, h = 0;
  getTextDimensions(string, &w, &h);
  return w > 0 || h > 0;
}

size_t StreamingEpdFont::getMemoryUsage() const {
  size_t usage = sizeof(StreamingEpdFont);
  usage += _glyphsSize;
  usage += _intervalsSize;
  usage += _totalCacheAllocation;
  return usage;
}

void StreamingEpdFont::logCacheStats() const {
  // No-op: debug logging removed
}
