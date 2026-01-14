#include "FontManager.h"

#include <EpdFontLoader.h>
#include <SDCardManager.h>

#include <cstring>

#include "config.h"

FontManager& FontManager::instance() {
  static FontManager instance;
  return instance;
}

FontManager::FontManager() = default;

FontManager::~FontManager() { unloadAllFonts(); }

void FontManager::init(GfxRenderer& r) { renderer = &r; }

bool FontManager::loadFontFamily(const char* familyName, int fontId) {
  if (!renderer || !familyName || !*familyName) {
    return false;
  }

  // Build base path
  char basePath[64];
  snprintf(basePath, sizeof(basePath), "%s/%s", CONFIG_FONTS_DIR, familyName);

  // Check if directory exists
  if (!SdMan.exists(basePath)) {
    Serial.printf("[FONT] Font family not found: %s\n", basePath);
    return false;
  }

  LoadedFamily family;
  family.fontId = fontId;

  // Try to load each style
  const char* styles[] = {"regular", "bold", "italic", "bold_italic"};
  EpdFont* fontPtrs[4] = {nullptr, nullptr, nullptr, nullptr};

  for (int i = 0; i < 4; i++) {
    char fontPath[80];
    snprintf(fontPath, sizeof(fontPath), "%s/%s.epdfont", basePath, styles[i]);

    LoadedFont loaded = loadSingleFont(fontPath);
    if (loaded.font) {
      family.fonts.push_back(loaded);
      fontPtrs[i] = loaded.font;
      Serial.printf("[FONT] Loaded %s/%s\n", familyName, styles[i]);
    }
  }

  // Need at least regular font
  if (!fontPtrs[0]) {
    // Free any loaded fonts
    for (auto& f : family.fonts) {
      freeFont(f);
    }
    Serial.printf("[FONT] Failed to load regular font for %s\n", familyName);
    return false;
  }

  // Create font family and register with renderer
  EpdFontFamily fontFamily(fontPtrs[0], fontPtrs[1], fontPtrs[2], fontPtrs[3]);
  renderer->insertFont(fontId, fontFamily);

  // Store for cleanup
  loadedFamilies[fontId] = std::move(family);

  Serial.printf("[FONT] Registered font family %s with ID %d\n", familyName, fontId);
  return true;
}

FontManager::LoadedFont FontManager::loadSingleFont(const char* path) {
  LoadedFont result = {nullptr, nullptr, nullptr, nullptr, nullptr};

  if (!SdMan.exists(path)) {
    return result;
  }

  EpdFontLoader::LoadResult loaded = EpdFontLoader::loadFromFile(path);
  if (!loaded.success) {
    Serial.printf("[FONT] Failed to load: %s\n", path);
    return result;
  }

  result.data = loaded.fontData;
  result.bitmap = loaded.bitmap;
  result.glyphs = loaded.glyphs;
  result.intervals = loaded.intervals;
  result.font = new EpdFont(result.data);

  return result;
}

void FontManager::freeFont(LoadedFont& font) {
  delete font.font;
  delete font.data;
  delete[] font.bitmap;
  delete[] font.glyphs;
  delete[] font.intervals;
  font = {nullptr, nullptr, nullptr, nullptr, nullptr};
}

void FontManager::unloadFontFamily(int fontId) {
  auto it = loadedFamilies.find(fontId);
  if (it != loadedFamilies.end()) {
    for (auto& f : it->second.fonts) {
      freeFont(f);
    }
    loadedFamilies.erase(it);
    Serial.printf("[FONT] Unloaded font family ID %d\n", fontId);
  }
}

void FontManager::unloadAllFonts() {
  for (auto& pair : loadedFamilies) {
    for (auto& f : pair.second.fonts) {
      freeFont(f);
    }
  }
  loadedFamilies.clear();
  Serial.println("[FONT] Unloaded all fonts");
}

std::vector<std::string> FontManager::listAvailableFonts() {
  std::vector<std::string> fonts;

  FsFile dir = SdMan.open(CONFIG_FONTS_DIR);
  if (!dir || !dir.isDirectory()) {
    return fonts;
  }

  FsFile entry;
  while (entry.openNext(&dir, O_RDONLY)) {
    if (entry.isDirectory()) {
      char name[64];
      entry.getName(name, sizeof(name));
      // Skip hidden directories
      if (name[0] != '.') {
        // Check if it has at least regular.epdfont
        char regularPath[80];
        snprintf(regularPath, sizeof(regularPath), "%s/%s/regular.epdfont", CONFIG_FONTS_DIR, name);
        if (SdMan.exists(regularPath)) {
          fonts.push_back(name);
        }
      }
    }
    entry.close();
  }
  dir.close();

  return fonts;
}

bool FontManager::fontFamilyExists(const char* familyName) {
  if (!familyName || !*familyName) return false;

  char path[80];
  snprintf(path, sizeof(path), "%s/%s/regular.epdfont", CONFIG_FONTS_DIR, familyName);
  return SdMan.exists(path);
}

int FontManager::getFontId(const char* familyName, int builtinFontId) {
  // Empty name means use builtin
  if (!familyName || !*familyName) {
    return builtinFontId;
  }

  // Check if already loaded
  int targetId = generateFontId(familyName);
  if (loadedFamilies.find(targetId) != loadedFamilies.end()) {
    return targetId;
  }

  // Try to load
  if (loadFontFamily(familyName, targetId)) {
    return targetId;
  }

  // Fallback to builtin
  return builtinFontId;
}

int FontManager::generateFontId(const char* familyName) {
  // Simple hash for consistent font IDs
  uint32_t hash = 5381;
  while (*familyName) {
    hash = ((hash << 5) + hash) + static_cast<uint8_t>(*familyName);
    familyName++;
  }
  return static_cast<int>(hash);
}
