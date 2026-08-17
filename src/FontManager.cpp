#include "FontManager.h"

#include <EpdFontLoader.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <StreamingEpdFont.h>
#include <esp_heap_caps.h>

#include <cstring>

#include "config.h"
#include "content/FileFingerprint.h"

#define TAG "FONT"

namespace {
constexpr uint32_t kFontFingerprintBasis = 2166136261u;
constexpr uint32_t kFontFingerprintPrime = 16777619u;

uint32_t updateFontFingerprint(uint32_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= kFontFingerprintPrime;
  }
  return hash;
}

uint32_t builtinFontFingerprint(int fontId) {
  return updateFontFingerprint(kFontFingerprintBasis, &fontId, sizeof(fontId));
}
}  // namespace

FontManager& FontManager::instance() {
  static FontManager instance;
  return instance;
}

FontManager::FontManager() = default;

FontManager::~FontManager() {
  unloadAllFonts();
  unloadExternalFont();
}

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
    LOG_ERR(TAG, "Custom font '%s' not found at %s", familyName, basePath);

    // Hint: detect the common mistake of dropping a raw .ttf/.otf/.epdfont next to the fonts dir
    const char* const rawExts[] = {".ttf", ".otf", ".epdfont"};
    for (const char* ext : rawExts) {
      char rawPath[80];
      snprintf(rawPath, sizeof(rawPath), "%s/%s%s", CONFIG_FONTS_DIR, familyName, ext);
      if (SdMan.exists(rawPath)) {
        LOG_ERR(TAG,
                "Hint: '%s' exists but fonts must live in '%s/regular.epdfont' - "
                "convert with scripts/fontconvert.py",
                rawPath, basePath);
        break;
      }
    }
    return false;
  }

  struct StyleInfo {
    const char* filename;
    EpdFontFamily::Style style;
  };
  const StyleInfo styles[] = {{"regular.epdfont", EpdFontFamily::REGULAR}, {"bold.epdfont", EpdFontFamily::BOLD}};

  LoadedFamily family;
  family.fontId = fontId;
  family.fingerprint = builtinFontFingerprint(fontId);

  char stylePaths[3][80] = {};
  for (const auto& s : styles) {
    char* fontPath = stylePaths[s.style];
    snprintf(fontPath, sizeof(stylePaths[s.style]), "%s/%s", basePath, s.filename);

    if (!SdMan.exists(fontPath)) {
      if (s.style == EpdFontFamily::REGULAR) {
        LOG_ERR(TAG, "Custom font '%s': missing required file regular.epdfont in %s", familyName, basePath);
        return false;  // Regular is required
      }
      fontPath[0] = '\0';
      continue;
    }

    const uint32_t styleFingerprint = papyrix::fileFingerprint(fontPath);
    if (styleFingerprint == 0) {
      LOG_ERR(TAG, "Failed to fingerprint font file: %s", fontPath);
      return false;
    }
    const uint8_t style = static_cast<uint8_t>(s.style);
    family.fingerprint = updateFontFingerprint(family.fingerprint, &style, sizeof(style));
    family.fingerprint = updateFontFingerprint(family.fingerprint, &styleFingerprint, sizeof(styleFingerprint));
  }

  for (const auto& s : styles) {
    const char* fontPath = stylePaths[s.style];
    if (!*fontPath) continue;

    // Defer bold/italic loading to save ~42KB per variant
    if (s.style != EpdFontFamily::REGULAR) {
      family.deferredPaths[s.style] = fontPath;
      continue;
    }

    LoadedFont loaded = _useStreamingFonts ? loadStreamingFont(fontPath) : loadSingleFont(fontPath);

    if (!loaded.font && !loaded.streamingFont) {
      LOG_ERR(TAG, "Custom font '%s': failed to load regular.epdfont (corrupt, truncated, or wrong format)",
              familyName);
      return false;  // Regular is required
    }

    // Create EpdFont wrapper if streaming
    if (loaded.streamingFont) {
      loaded.font = new (std::nothrow) EpdFont(loaded.streamingFont->getData());
      if (!loaded.font) {
        delete loaded.streamingFont;
        loaded.streamingFont = nullptr;
        LOG_ERR(TAG, "Custom font '%s': out of memory wrapping streaming font", familyName);
        return false;
      }
      renderer->setStreamingFont(fontId, s.style, loaded.streamingFont);
    }

    family.fonts[s.style] = loaded;
  }

  // Register with renderer: bold initially nullptr (loaded on demand)
  // Italic not provided for custom fonts — falls back to regular via EpdFontFamily::getFont()
  EpdFontFamily fontFamily(family.fonts[0].font, nullptr, nullptr, nullptr);
  renderer->insertFont(fontId, fontFamily);

  // Set up lazy-loading resolver so bold loads on first use
  renderer->setFontStyleResolver(fontStyleResolverCallback, this);

  // Store for cleanup
  loadedFamilies[fontId] = std::move(family);
  return true;
}

FontManager::LoadedFont FontManager::loadSingleFont(const char* path) {
  LoadedFont result = {};

  if (!SdMan.exists(path)) {
    return result;
  }

  EpdFontLoader::LoadResult loaded = EpdFontLoader::loadFromFile(path);
  if (!loaded.success) {
    return result;
  }

  result.data = loaded.fontData;
  result.bitmap = loaded.bitmap;
  result.glyphs = loaded.glyphs;
  result.intervals = loaded.intervals;
  result.font = new EpdFont(result.data);

  // Store sizes for memory profiling
  result.bitmapSize = loaded.bitmapSize;
  result.glyphsSize = loaded.glyphsSize;
  result.intervalsSize = loaded.intervalsSize;

  return result;
}

FontManager::LoadedFont FontManager::loadStreamingFont(const char* path) {
  LoadedFont result = {};

  if (!SdMan.exists(path)) {
    return result;
  }

  StreamingEpdFont* streamingFont = new (std::nothrow) StreamingEpdFont();
  if (!streamingFont) {
    return result;
  }

  if (!streamingFont->load(path)) {
    delete streamingFont;
    return result;
  }

  result.streamingFont = streamingFont;
  // glyphsSize and intervalsSize are tracked inside StreamingEpdFont
  // totalSize() will use streamingFont->getMemoryUsage()

  return result;
}

void FontManager::freeFont(LoadedFont& font) {
  // Free streaming font if present
  if (font.streamingFont) {
    delete font.streamingFont;
    font.streamingFont = nullptr;
  }

  // Free full-load font resources if present
  delete font.font;
  delete font.data;
  delete[] font.bitmap;
  delete[] font.glyphs;
  delete[] font.intervals;

  font = {};
}

void FontManager::unloadFontFamily(int fontId) {
  auto it = loadedFamilies.find(fontId);
  if (it != loadedFamilies.end()) {
    if (renderer) {
      renderer->removeFont(fontId);
    }
    for (auto& f : it->second.fonts) {
      freeFont(f);
    }
    loadedFamilies.erase(it);
  }
}

void FontManager::unloadAllFonts() {
  for (auto& pair : loadedFamilies) {
    if (renderer) {
      renderer->removeFont(pair.second.fontId);
    }
    for (auto& f : pair.second.fonts) {
      freeFont(f);
    }
  }
  loadedFamilies.clear();
}

void FontManager::fontStyleResolverCallback(void* ctx, int fontId, int styleIdx) {
  auto* self = static_cast<FontManager*>(ctx);
  self->loadDeferredStyle(fontId, styleIdx);
}

void FontManager::loadDeferredStyle(int fontId, int styleIdx) {
  if (styleIdx < 0 || styleIdx > 2) return;

  auto famIt = loadedFamilies.find(fontId);
  if (famIt == loadedFamilies.end()) return;

  auto& family = famIt->second;
  if (family.deferredPaths[styleIdx].empty()) return;

  std::string path = std::move(family.deferredPaths[styleIdx]);
  family.deferredPaths[styleIdx].clear();

  LoadedFont loaded = _useStreamingFonts ? loadStreamingFont(path.c_str()) : loadSingleFont(path.c_str());
  if (!loaded.font && !loaded.streamingFont) return;

  auto style = static_cast<EpdFontFamily::Style>(styleIdx);

  if (loaded.streamingFont) {
    loaded.font = new (std::nothrow) EpdFont(loaded.streamingFont->getData());
    if (!loaded.font) {
      delete loaded.streamingFont;
      loaded.streamingFont = nullptr;
      return;
    }
    renderer->setStreamingFont(fontId, style, loaded.streamingFont);
  }

  // Update the EpdFontFamily in the renderer so glyph metrics are correct
  renderer->updateFontFamily(fontId, style, loaded.font);

  family.fonts[styleIdx] = loaded;
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
  if (!familyName || !*familyName) {
    return builtinFontId;
  }

  int targetId = generateFontId(familyName);
  if (loadedFamilies.find(targetId) != loadedFamilies.end()) {
    return targetId;
  }

  // Load from SD card
  if (loadFontFamily(familyName, targetId)) {
    return targetId;
  }

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

bool FontManager::isBinFont(const char* familyName) {
  if (!familyName) return false;
  size_t len = strlen(familyName);
  return len > 4 && strcmp(familyName + len - 4, ".bin") == 0;
}

int FontManager::getReaderFontId(const char* familyName, int builtinFontId) {
  const uint32_t builtinFingerprint = builtinFontFingerprint(builtinFontId);
  if (!familyName || !*familyName) {
    // Using built-in font - unload any custom reader font and external font
    if (_activeReaderFontId != 0 && _activeReaderFontId != builtinFontId) {
      unloadFontFamily(_activeReaderFontId);
    }
    _activeReaderFontId = 0;
    _activeReaderFontFingerprint = builtinFingerprint;
    _activeBinFontName[0] = '\0';
    _activeBinBuiltinFontId = 0;
    unloadExternalFont();
    return builtinFontId;
  }

  // Handle .bin fonts as external fonts (CJK fallback)
  if (isBinFont(familyName)) {
    if (_activeBinBuiltinFontId == builtinFontId && strcmp(_activeBinFontName, familyName) == 0) {
      return builtinFontId;
    }

    if (_activeReaderFontId != 0 && _activeReaderFontId != builtinFontId) {
      unloadFontFamily(_activeReaderFontId);
    }
    _activeReaderFontId = 0;
    unloadExternalFont();

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", CONFIG_FONTS_DIR, familyName);
    const uint32_t fileSignature = papyrix::fileFingerprint(path);
    const uint32_t cacheSignature = papyrix::fingerprintOrSession(fileSignature);
    if (fileSignature == 0) {
      LOG_WRN(TAG, "External font fingerprint unavailable; using session cache ID for %s", path);
    }
    _activeReaderFontFingerprint = updateFontFingerprint(builtinFingerprint, familyName, strlen(familyName));
    _activeReaderFontFingerprint =
        updateFontFingerprint(_activeReaderFontFingerprint, &cacheSignature, sizeof(cacheSignature));
    strncpy(_activeBinFontName, familyName, sizeof(_activeBinFontName) - 1);
    _activeBinFontName[sizeof(_activeBinFontName) - 1] = '\0';
    _activeBinBuiltinFontId = builtinFontId;

    // Defer external font loading until a CJK character is actually encountered.
    deferExternalFont(familyName);
    return builtinFontId;
  }

  if (_activeBinFontName[0] != '\0') {
    _activeBinFontName[0] = '\0';
    _activeBinBuiltinFontId = 0;
    unloadExternalFont();
  }

  const int targetId = generateFontId(familyName);
  if (_activeReaderFontId != 0 && _activeReaderFontId != targetId) {
    unloadFontFamily(_activeReaderFontId);
  }

  auto loaded = loadedFamilies.find(targetId);
  if (loaded == loadedFamilies.end()) {
    if (!loadFontFamily(familyName, targetId)) {
      _activeReaderFontId = 0;
      _activeReaderFontFingerprint = builtinFingerprint;
      return builtinFontId;
    }
    loaded = loadedFamilies.find(targetId);
  }

  _activeReaderFontId = targetId;
  _activeReaderFontFingerprint = loaded->second.fingerprint;
  return targetId;
}

bool FontManager::loadExternalFont(const char* filename) {
  if (!renderer || !filename || !*filename) {
    return false;
  }

  char path[80];
  snprintf(path, sizeof(path), "%s/%s", CONFIG_FONTS_DIR, filename);

  // Allocate if needed
  if (!_externalFont) {
    _externalFont = new ExternalFont();
  }

  if (!_externalFont->load(path)) {
    delete _externalFont;
    _externalFont = nullptr;
    return false;
  }

  renderer->setExternalFont(_externalFont);
  return true;
}

void FontManager::deferExternalFont(const char* filename) {
  if (!renderer || !filename || !*filename) return;

  strncpy(_deferredExternalFontName, filename, sizeof(_deferredExternalFontName) - 1);
  _deferredExternalFontName[sizeof(_deferredExternalFontName) - 1] = '\0';

  renderer->setExternalFontResolver(externalFontResolverCallback, this);
  LOG_DBG(TAG, "Deferred external font: %s", filename);
}

void FontManager::externalFontResolverCallback(void* ctx) {
  auto* self = static_cast<FontManager*>(ctx);
  if (self->_deferredExternalFontName[0] != '\0') {
    LOG_INF(TAG, "Lazy-loading external font: %s", self->_deferredExternalFontName);
    self->loadExternalFont(self->_deferredExternalFontName);
    self->_deferredExternalFontName[0] = '\0';
  }
}

void FontManager::unloadExternalFont() {
  _deferredExternalFontName[0] = '\0';
  if (renderer) {
    renderer->setExternalFontResolver(nullptr, nullptr);
  }
  if (_externalFont) {
    if (renderer) {
      renderer->setExternalFont(nullptr);
    }
    delete _externalFont;
    _externalFont = nullptr;
  }
}

void FontManager::logFontInfo() const {
  // No-op: debug logging removed
}

void FontManager::logMemoryStatus(const char*) const {
  // No-op: debug logging removed
}

void FontManager::unloadReaderFonts() {
  // Unload any custom .epdfont reader font
  if (_activeReaderFontId != 0) {
    unloadFontFamily(_activeReaderFontId);
    _activeReaderFontId = 0;
  }

  _activeBinFontName[0] = '\0';
  _activeBinBuiltinFontId = 0;

  // Unload external CJK font
  unloadExternalFont();
}

size_t FontManager::getCustomFontMemoryUsage() const {
  size_t total = 0;
  for (const auto& pair : loadedFamilies) {
    for (const auto& font : pair.second.fonts) {
      if (font.font || font.streamingFont) {
        total += font.totalSize();
      }
    }
  }
  return total;
}

size_t FontManager::getExternalFontMemoryUsage() const {
  if (_externalFont && _externalFont->isLoaded()) {
    return ExternalFont::getCacheMemorySize();
  }
  return 0;
}

size_t FontManager::getTotalFontMemoryUsage() const {
  return getCustomFontMemoryUsage() + getExternalFontMemoryUsage();
}

void FontManager::logMemoryReport() const {
  // No-op: debug logging removed
}
