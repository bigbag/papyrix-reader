#pragma once

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>

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
   *   - italic.epdfont (optional)
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
   * Generate a unique font ID for a family name.
   * Uses hash of the name for consistency.
   */
  static int generateFontId(const char* familyName);

 private:
  FontManager();
  ~FontManager();
  FontManager(const FontManager&) = delete;
  FontManager& operator=(const FontManager&) = delete;

  GfxRenderer* renderer = nullptr;

  // Track loaded fonts for cleanup
  struct LoadedFont {
    EpdFont* font;
    EpdFontData* data;
    uint8_t* bitmap;
    EpdGlyph* glyphs;
    EpdUnicodeInterval* intervals;
  };

  struct LoadedFamily {
    std::vector<LoadedFont> fonts;  // Up to 4: regular, bold, italic, bold_italic
    int fontId;
  };

  std::map<int, LoadedFamily> loadedFamilies;

  LoadedFont loadSingleFont(const char* path);
  void freeFont(LoadedFont& font);
};

// Convenience macro
#define FONT_MANAGER FontManager::instance()
