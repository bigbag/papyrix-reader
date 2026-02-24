#pragma once

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <ExternalFont.h>
#include <GfxRenderer.h>
#include <StreamingEpdFont.h>

#include <map>
#include <string>
#include <vector>

// Forward declaration
class EpdFontLoader;

/**
 * Singleton manager for dynamic font loading from SD card.
 *
 * Loads .epdfont binary files from /fonts/ directory.
 * Falls back to builtin fonts when external fonts are unavailable.
 *
 * Usage:
 *   FONT_MANAGER.init(renderer);
 *   FONT_MANAGER.loadFontFamily("noto-serif", CUSTOM_FONT_ID);
 *   renderer.drawText(CUSTOM_FONT_ID, x, y, "Hello");
 */
class FontManager {
 public:
  static FontManager& instance();

  /**
   * Initialize the font manager with a renderer reference.
   * Must be called before loading fonts.
   */
  void init(GfxRenderer& renderer);

  /**
   * Load a font family from SD card.
   * Looks for files in /fonts/<familyName>/:
   *   - regular.epdfont
   *   - bold.epdfont (optional)
   *
   * @param familyName Directory name under /fonts/
   * @param fontId Unique ID to register with renderer
   * @return true if at least the regular font was loaded
   */
  bool loadFontFamily(const char* familyName, int fontId);

  /**
   * Unload a font family and free memory.
   * @param fontId The font ID to unload
   */
  void unloadFontFamily(int fontId);

  /**
   * Unload all dynamically loaded fonts.
   */
  void unloadAllFonts();

  /**
   * List available font families on SD card.
   * @return Vector of family names (directory names under /fonts/)
   */
  std::vector<std::string> listAvailableFonts();

  /**
   * Check if a font family exists on SD card.
   */
  bool fontFamilyExists(const char* familyName);

  /**
   * Get font ID for a font family name.
   * Returns builtin font ID if external font not found.
   *
   * @param familyName Font family name (empty = builtin)
   * @param builtinFontId Fallback font ID
   * @return Font ID to use
   */
  int getFontId(const char* familyName, int builtinFontId);

  /**
   * Get font ID for reader fonts with automatic cleanup of previous fonts.
   * This prevents memory leaks when switching between font sizes.
   *
   * @param familyName Font family name (empty = builtin)
   * @param builtinFontId Fallback font ID
   * @return Font ID to use
   */
  int getReaderFontId(const char* familyName, int builtinFontId);

  /**
   * Generate a unique font ID for a family name.
   * Uses hash of the name for consistency.
   */
  static int generateFontId(const char* familyName);

  /**
   * Check if a font family name refers to a .bin external font.
   */
  static bool isBinFont(const char* familyName);

  // External font (CJK fallback) support
  /**
   * Load an external font (.bin format) for CJK character fallback.
   * @param filename Filename under /config/fonts/ (e.g., "NotoSansCJK_24_24x26.bin")
   * @return true if loaded successfully
   */
  bool loadExternalFont(const char* filename);

  /**
   * Defer external font loading until a CJK character is actually encountered.
   * Registers a lazy-load callback with GfxRenderer instead of loading immediately.
   * Saves ~13KB for non-CJK books.
   * @param filename Filename under /config/fonts/
   */
  void deferExternalFont(const char* filename);

  /**
   * Unload the external font and free memory.
   */
  void unloadExternalFont();

  /**
   * Get the external font pointer (may be nullptr).
   */
  ExternalFont* getExternalFont() { return (_externalFont && _externalFont->isLoaded()) ? _externalFont : nullptr; }

  /// Returns true if the active reader font was loaded from SD card (not a builtin font).
  bool isUsingCustomReaderFont() const { return _activeReaderFontId != 0; }

  /**
   * Log information about all loaded fonts.
   */
  void logFontInfo() const;

  /**
   * Log current memory status for profiling.
   * Logs free heap size and largest free block.
   * @param context Description of when this is called (e.g., "before font load")
   */
  void logMemoryStatus(const char* context) const;

  /**
   * Unload the active reader font and external font.
   * Call this when leaving reader mode to free memory.
   */
  void unloadReaderFonts();

  /**
   * Get total RAM usage by all loaded custom fonts (from SD card).
   * Does not include built-in fonts (they are in Flash).
   * @return Total bytes used by custom fonts
   */
  size_t getCustomFontMemoryUsage() const;

  /**
   * Get total RAM usage by the external CJK font cache.
   * @return Bytes used by ExternalFont cache, or 0 if not loaded
   */
  size_t getExternalFontMemoryUsage() const;

  /**
   * Get total font-related RAM usage.
   * @return Total bytes used by all font systems
   */
  size_t getTotalFontMemoryUsage() const;

  /**
   * Log detailed memory profiling report for all font subsystems.
   * Includes per-font breakdown, cache stats, and heap status.
   */
  void logMemoryReport() const;

 private:
  FontManager();
  ~FontManager();
  FontManager(const FontManager&) = delete;
  FontManager& operator=(const FontManager&) = delete;

  GfxRenderer* renderer = nullptr;

  // Track loaded fonts for cleanup and memory profiling
  struct LoadedFont {
    // Either full font OR streaming font (not both)
    EpdFont* font = nullptr;
    StreamingEpdFont* streamingFont = nullptr;

    // Full-load mode resources (font != nullptr)
    EpdFontData* data = nullptr;
    uint8_t* bitmap = nullptr;
    EpdGlyph* glyphs = nullptr;
    EpdUnicodeInterval* intervals = nullptr;

    // Memory tracking (in bytes)
    size_t bitmapSize = 0;
    size_t glyphsSize = 0;
    size_t intervalsSize = 0;

    bool isStreaming() const { return streamingFont != nullptr; }

    size_t totalSize() const {
      if (streamingFont) {
        return streamingFont->getMemoryUsage();
      }
      return bitmapSize + glyphsSize + intervalsSize + sizeof(EpdFontData) + sizeof(EpdFont);
    }
  };

  struct LoadedFamily {
    LoadedFont fonts[3];           // Indexed by Style: REGULAR=0, BOLD=1, ITALIC=2
    std::string deferredPaths[3];  // Paths for lazy loading (empty = not available)
    int fontId = 0;
  };

  std::map<int, LoadedFamily> loadedFamilies;

  // Track active reader font for cleanup when switching sizes
  int _activeReaderFontId = 0;

  // External font for CJK fallback (pointer to avoid 54KB allocation when unused)
  ExternalFont* _externalFont = nullptr;

  // Deferred external font filename (for lazy loading on first CJK codepoint)
  char _deferredExternalFontName[48] = {};
  static void externalFontResolverCallback(void* ctx);

  LoadedFont loadSingleFont(const char* path);
  LoadedFont loadStreamingFont(const char* path);
  void freeFont(LoadedFont& font);
  void loadDeferredStyle(int fontId, int styleIdx);

  static void fontStyleResolverCallback(void* ctx, int fontId, int styleIdx);

  // Whether to use streaming fonts (default: true for memory savings)
  bool _useStreamingFonts = true;
};

// Convenience macro
#define FONT_MANAGER FontManager::instance()
