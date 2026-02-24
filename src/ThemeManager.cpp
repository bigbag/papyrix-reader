#include "ThemeManager.h"

#include <Logging.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>

#include "IniParser.h"
#include "config.h"

#define TAG "THEME"

ThemeManager& ThemeManager::instance() {
  static ThemeManager instance;
  return instance;
}

ThemeManager::ThemeManager() : activeTheme(BUILTIN_LIGHT_THEME) { strncpy(themeName, "light", sizeof(themeName)); }

bool ThemeManager::loadTheme(const char* name) {
  if (!name || !*name) {
    applyLightTheme();
    return false;
  }

  // Build path
  char path[128];
  snprintf(path, sizeof(path), "%s/%s.theme", CONFIG_THEMES_DIR, name);

  // Try to load from file
  if (loadFromFile(path)) {
    strncpy(themeName, name, sizeof(themeName) - 1);
    themeName[sizeof(themeName) - 1] = '\0';
    return true;
  }

  // Fallback to builtin themes
  if (strcmp(name, "dark") == 0) {
    applyDarkTheme();
  } else {
    applyLightTheme();
  }

  strncpy(themeName, name, sizeof(themeName) - 1);
  themeName[sizeof(themeName) - 1] = '\0';
  return false;
}

bool ThemeManager::loadFromFile(const char* path) { return loadFromFileToTheme(path, activeTheme); }

bool ThemeManager::loadFromFileToTheme(const char* path, Theme& theme) {
  // Start with light theme defaults
  theme = BUILTIN_LIGHT_THEME;

  return IniParser::parseFile(path, [&theme](const char* section, const char* key, const char* value) {
    // [theme] section - metadata
    if (strcmp(section, "theme") == 0) {
      if (strcmp(key, "name") == 0) {
        strncpy(theme.displayName, value, sizeof(theme.displayName) - 1);
        theme.displayName[sizeof(theme.displayName) - 1] = '\0';
      }
    }
    // [colors] section
    else if (strcmp(section, "colors") == 0) {
      if (strcmp(key, "inverted_mode") == 0) {
        theme.invertedMode = IniParser::parseBool(value, false);
      } else if (strcmp(key, "background") == 0) {
        theme.backgroundColor = IniParser::parseColor(value, 0xFF);
      }
    }
    // [selection] section
    else if (strcmp(section, "selection") == 0) {
      if (strcmp(key, "fill_color") == 0) {
        theme.selectionFillBlack = (IniParser::parseColor(value, 0x00) == 0x00);
      } else if (strcmp(key, "text_color") == 0) {
        theme.selectionTextBlack = (IniParser::parseColor(value, 0xFF) == 0x00);
      }
    }
    // [text] section
    else if (strcmp(section, "text") == 0) {
      if (strcmp(key, "primary_color") == 0) {
        theme.primaryTextBlack = (IniParser::parseColor(value, 0x00) == 0x00);
      } else if (strcmp(key, "secondary_color") == 0) {
        theme.secondaryTextBlack = (IniParser::parseColor(value, 0x00) == 0x00);
      }
    }
    // [layout] section
    else if (strcmp(section, "layout") == 0) {
      if (strcmp(key, "margin_top") == 0) {
        theme.screenMarginTop = static_cast<uint8_t>(IniParser::parseInt(value, 9));
      } else if (strcmp(key, "margin_side") == 0) {
        theme.screenMarginSide = static_cast<uint8_t>(IniParser::parseInt(value, 3));
      } else if (strcmp(key, "item_height") == 0) {
        theme.itemHeight = static_cast<uint8_t>(IniParser::parseInt(value, 30));
      } else if (strcmp(key, "item_spacing") == 0) {
        theme.itemSpacing = static_cast<uint8_t>(IniParser::parseInt(value, 0));
      } else if (strcmp(key, "item_padding_x") == 0) {
        theme.itemPaddingX = static_cast<uint8_t>(IniParser::parseInt(value, 8));
      } else if (strcmp(key, "item_value_padding") == 0) {
        theme.itemValuePadding = static_cast<uint8_t>(IniParser::parseInt(value, 20));
      }
    }
    // [fonts] section
    else if (strcmp(section, "fonts") == 0) {
      if (strcmp(key, "ui_font") == 0) {
        strncpy(theme.uiFontFamily, value, sizeof(theme.uiFontFamily) - 1);
        theme.uiFontFamily[sizeof(theme.uiFontFamily) - 1] = '\0';
      } else if (strcmp(key, "reader_font_xsmall") == 0) {
        strncpy(theme.readerFontFamilyXSmall, value, sizeof(theme.readerFontFamilyXSmall) - 1);
        theme.readerFontFamilyXSmall[sizeof(theme.readerFontFamilyXSmall) - 1] = '\0';
      } else if (strcmp(key, "reader_font_small") == 0) {
        strncpy(theme.readerFontFamilySmall, value, sizeof(theme.readerFontFamilySmall) - 1);
        theme.readerFontFamilySmall[sizeof(theme.readerFontFamilySmall) - 1] = '\0';
      } else if (strcmp(key, "reader_font_medium") == 0) {
        strncpy(theme.readerFontFamilyMedium, value, sizeof(theme.readerFontFamilyMedium) - 1);
        theme.readerFontFamilyMedium[sizeof(theme.readerFontFamilyMedium) - 1] = '\0';
      } else if (strcmp(key, "reader_font_large") == 0) {
        strncpy(theme.readerFontFamilyLarge, value, sizeof(theme.readerFontFamilyLarge) - 1);
        theme.readerFontFamilyLarge[sizeof(theme.readerFontFamilyLarge) - 1] = '\0';
      }
    }

    return true;  // Continue parsing
  });
}

bool ThemeManager::applyCachedTheme(const char* themeName) {
  if (!themeName || !*themeName) return false;

  auto it = themeCache.find(themeName);
  if (it != themeCache.end()) {
    activeTheme = it->second;
    strncpy(this->themeName, themeName, sizeof(this->themeName) - 1);
    this->themeName[sizeof(this->themeName) - 1] = '\0';
    return true;
  }
  return false;
}

bool ThemeManager::isThemeCached(const char* themeName) const {
  if (!themeName || !*themeName) return false;
  return themeCache.find(themeName) != themeCache.end();
}

bool ThemeManager::saveTheme(const char* name) {
  if (!name || !*name) return false;

  // Ensure themes directory exists
  if (!SdMan.exists(CONFIG_THEMES_DIR)) {
    SdMan.mkdir(CONFIG_THEMES_DIR);
  }

  char path[128];
  snprintf(path, sizeof(path), "%s/%s.theme", CONFIG_THEMES_DIR, name);

  return saveToFile(path, activeTheme);
}

bool ThemeManager::saveToFile(const char* path, const Theme& theme) {
  FsFile file = SdMan.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return false;

  file.println("# Papyrix Theme Configuration");
  file.println("# Edit values and restart device to apply");
  file.println();

  file.println("[theme]");
  file.printf("name = %s\n", theme.displayName[0] ? theme.displayName : "Custom");
  file.println();

  file.println("[colors]");
  file.printf("inverted_mode = %s\n", theme.invertedMode ? "true" : "false");
  file.printf("background = %s\n", theme.backgroundColor == 0x00 ? "black" : "white");
  file.println();

  file.println("[selection]");
  file.printf("fill_color = %s\n", theme.selectionFillBlack ? "black" : "white");
  file.printf("text_color = %s\n", theme.selectionTextBlack ? "black" : "white");
  file.println();

  file.println("[text]");
  file.printf("primary_color = %s\n", theme.primaryTextBlack ? "black" : "white");
  file.printf("secondary_color = %s\n", theme.secondaryTextBlack ? "black" : "white");
  file.println();

  file.println("[layout]");
  file.printf("margin_top = %d\n", theme.screenMarginTop);
  file.printf("margin_side = %d\n", theme.screenMarginSide);
  file.printf("item_height = %d\n", theme.itemHeight);
  file.printf("item_spacing = %d\n", theme.itemSpacing);
  file.printf("item_padding_x = %d\n", theme.itemPaddingX);
  file.printf("item_value_padding = %d\n", theme.itemValuePadding);
  file.println();

  file.println("[fonts]");
  file.printf("ui_font = %s\n", theme.uiFontFamily);
  file.printf("reader_font_xsmall = %s\n", theme.readerFontFamilyXSmall);
  file.printf("reader_font_small = %s\n", theme.readerFontFamilySmall);
  file.printf("reader_font_medium = %s\n", theme.readerFontFamilyMedium);
  file.printf("reader_font_large = %s\n", theme.readerFontFamilyLarge);

  file.close();
  return true;
}

void ThemeManager::applyLightTheme() {
  activeTheme = BUILTIN_LIGHT_THEME;
  strncpy(themeName, "light", sizeof(themeName));
}

void ThemeManager::applyDarkTheme() {
  activeTheme = BUILTIN_DARK_THEME;
  strncpy(themeName, "dark", sizeof(themeName));
}

std::vector<std::string> ThemeManager::listAvailableThemes(bool forceRefresh) {
  std::vector<std::string> themes;

  // Only rebuild cache if explicitly requested
  if (forceRefresh) {
    themeCache.clear();
  }

  // Always include builtin themes and cache them
  themes.push_back("light");
  themes.push_back("dark");
  themeCache["light"] = BUILTIN_LIGHT_THEME;
  themeCache["dark"] = BUILTIN_DARK_THEME;

  // List theme files from SD
  FsFile dir = SdMan.open(CONFIG_THEMES_DIR);
  if (!dir || !dir.isDirectory()) {
    return themes;
  }

  auto isValidThemeName = [](const char* name, size_t len) -> bool {
    for (size_t i = 0; i < len; i++) {
      char c = name[i];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
        return false;
      }
    }
    return len > 0;
  };

  FsFile entry;
  while (entry.openNext(&dir, O_RDONLY)) {
    if (!entry.isDirectory()) {
      char name[64];
      entry.getName(name, sizeof(name));
      size_t len = strlen(name);

      // Check for .theme extension
      if (len > 6 && strcmp(name + len - 6, ".theme") == 0) {
        size_t nameLen = len - 6;

        // Skip themes with names too long for buffer
        char themeNameBuf[32];
        if (nameLen >= sizeof(themeNameBuf)) {
          LOG_INF(TAG, "Skipping theme with name too long: %s", name);
          entry.close();
          continue;
        }

        strncpy(themeNameBuf, name, nameLen);
        themeNameBuf[nameLen] = '\0';

        if (!isValidThemeName(themeNameBuf, nameLen)) {
          LOG_INF(TAG, "Skipping theme with invalid name: %s", name);
          entry.close();
          continue;
        }

        // Add if not already in list (avoid duplicating light/dark)
        if (!std::any_of(themes.begin(), themes.end(), [&](const std::string& t) { return t == themeNameBuf; })) {
          // Only parse if not already cached
          if (themeCache.find(themeNameBuf) == themeCache.end()) {
            char path[128];
            snprintf(path, sizeof(path), "%s/%s", CONFIG_THEMES_DIR, name);
            Theme cachedTheme;
            if (loadFromFileToTheme(path, cachedTheme)) {
              themeCache[themeNameBuf] = cachedTheme;
            } else {
              LOG_ERR(TAG, "Failed to load theme '%s', skipping", themeNameBuf);
              entry.close();
              continue;
            }
          }

          themes.push_back(themeNameBuf);

          // Stop if we've reached the maximum theme limit
          if (themes.size() >= MAX_CACHED_THEMES) {
            LOG_INF(TAG, "Maximum theme limit (%d) reached, skipping remaining", MAX_CACHED_THEMES);
            entry.close();
            break;
          }
        }
      }
    }
    entry.close();
  }
  dir.close();

  return themes;
}

void ThemeManager::createDefaultThemeFiles() {
  // Ensure themes directory exists
  if (!SdMan.exists(CONFIG_THEMES_DIR)) {
    SdMan.mkdir(CONFIG_THEMES_DIR);
  }

  // Create light.theme if it doesn't exist
  char lightPath[128];
  snprintf(lightPath, sizeof(lightPath), "%s/light.theme", CONFIG_THEMES_DIR);
  if (!SdMan.exists(lightPath)) {
    saveToFile(lightPath, BUILTIN_LIGHT_THEME);
  }

  // Create dark.theme if it doesn't exist
  char darkPath[128];
  snprintf(darkPath, sizeof(darkPath), "%s/dark.theme", CONFIG_THEMES_DIR);
  if (!SdMan.exists(darkPath)) {
    saveToFile(darkPath, BUILTIN_DARK_THEME);
  }
}
