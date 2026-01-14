#include "ThemeManager.h"

#include <SDCardManager.h>

#include <algorithm>
#include <cstring>

#include "IniParser.h"
#include "config.h"

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
  char path[64];
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

bool ThemeManager::loadFromFile(const char* path) {
  // Start with light theme defaults
  activeTheme = BUILTIN_LIGHT_THEME;

  return IniParser::parseFile(path, [this](const char* section, const char* key, const char* value) {
    // [theme] section - metadata
    if (strcmp(section, "theme") == 0) {
      if (strcmp(key, "name") == 0) {
        strncpy(activeTheme.displayName, value, sizeof(activeTheme.displayName) - 1);
        activeTheme.displayName[sizeof(activeTheme.displayName) - 1] = '\0';
      }
    }
    // [colors] section
    else if (strcmp(section, "colors") == 0) {
      if (strcmp(key, "inverted_mode") == 0) {
        activeTheme.invertedMode = IniParser::parseBool(value, false);
      } else if (strcmp(key, "background") == 0) {
        activeTheme.backgroundColor = IniParser::parseColor(value, 0xFF);
      }
    }
    // [selection] section
    else if (strcmp(section, "selection") == 0) {
      if (strcmp(key, "fill_color") == 0) {
        activeTheme.selectionFillBlack = (IniParser::parseColor(value, 0x00) == 0x00);
      } else if (strcmp(key, "text_color") == 0) {
        activeTheme.selectionTextBlack = (IniParser::parseColor(value, 0xFF) == 0x00);
      }
    }
    // [text] section
    else if (strcmp(section, "text") == 0) {
      if (strcmp(key, "primary_color") == 0) {
        activeTheme.primaryTextBlack = (IniParser::parseColor(value, 0x00) == 0x00);
      } else if (strcmp(key, "secondary_color") == 0) {
        activeTheme.secondaryTextBlack = (IniParser::parseColor(value, 0x00) == 0x00);
      }
    }
    // [layout] section
    else if (strcmp(section, "layout") == 0) {
      if (strcmp(key, "margin_top") == 0) {
        activeTheme.screenMarginTop = static_cast<uint8_t>(IniParser::parseInt(value, 9));
      } else if (strcmp(key, "margin_side") == 0) {
        activeTheme.screenMarginSide = static_cast<uint8_t>(IniParser::parseInt(value, 3));
      } else if (strcmp(key, "item_height") == 0) {
        activeTheme.itemHeight = static_cast<uint8_t>(IniParser::parseInt(value, 30));
      } else if (strcmp(key, "item_spacing") == 0) {
        activeTheme.itemSpacing = static_cast<uint8_t>(IniParser::parseInt(value, 0));
      } else if (strcmp(key, "front_buttons") == 0) {
        activeTheme.frontButtonLayout = (strcmp(value, "lrbc") == 0) ? FRONT_LRBC : FRONT_BCLR;
      }
    }
    // [fonts] section
    else if (strcmp(section, "fonts") == 0) {
      if (strcmp(key, "ui_font") == 0) {
        strncpy(activeTheme.uiFontFamily, value, sizeof(activeTheme.uiFontFamily) - 1);
        activeTheme.uiFontFamily[sizeof(activeTheme.uiFontFamily) - 1] = '\0';
      } else if (strcmp(key, "reader_font_small") == 0) {
        strncpy(activeTheme.readerFontFamilySmall, value, sizeof(activeTheme.readerFontFamilySmall) - 1);
        activeTheme.readerFontFamilySmall[sizeof(activeTheme.readerFontFamilySmall) - 1] = '\0';
      } else if (strcmp(key, "reader_font_medium") == 0) {
        strncpy(activeTheme.readerFontFamilyMedium, value, sizeof(activeTheme.readerFontFamilyMedium) - 1);
        activeTheme.readerFontFamilyMedium[sizeof(activeTheme.readerFontFamilyMedium) - 1] = '\0';
      } else if (strcmp(key, "reader_font_large") == 0) {
        strncpy(activeTheme.readerFontFamilyLarge, value, sizeof(activeTheme.readerFontFamilyLarge) - 1);
        activeTheme.readerFontFamilyLarge[sizeof(activeTheme.readerFontFamilyLarge) - 1] = '\0';
      }
    }

    return true;  // Continue parsing
  });
}

bool ThemeManager::saveTheme(const char* name) {
  if (!name || !*name) return false;

  // Ensure themes directory exists
  if (!SdMan.exists(CONFIG_THEMES_DIR)) {
    SdMan.mkdir(CONFIG_THEMES_DIR);
  }

  char path[64];
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
  file.printf("front_buttons = %s\n", theme.frontButtonLayout == FRONT_LRBC ? "lrbc" : "bclr");
  file.println();

  file.println("[fonts]");
  file.printf("ui_font = %s\n", theme.uiFontFamily);
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

std::vector<std::string> ThemeManager::listAvailableThemes() {
  std::vector<std::string> themes;

  // Always include builtin themes
  themes.push_back("light");
  themes.push_back("dark");

  // List theme files from SD
  FsFile dir = SdMan.open(CONFIG_THEMES_DIR);
  if (!dir || !dir.isDirectory()) {
    return themes;
  }

  FsFile entry;
  while (entry.openNext(&dir, O_RDONLY)) {
    if (!entry.isDirectory()) {
      char name[64];
      entry.getName(name, sizeof(name));
      size_t len = strlen(name);

      // Check for .theme extension
      if (len > 6 && strcmp(name + len - 6, ".theme") == 0) {
        // Extract name without extension
        char themeNameBuf[32];
        size_t nameLen = len - 6;
        if (nameLen >= sizeof(themeNameBuf)) nameLen = sizeof(themeNameBuf) - 1;
        strncpy(themeNameBuf, name, nameLen);
        themeNameBuf[nameLen] = '\0';

        // Add if not already in list (avoid duplicating light/dark)
        if (!std::any_of(themes.begin(), themes.end(), [&](const std::string& t) { return t == themeNameBuf; })) {
          themes.push_back(themeNameBuf);
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
  char lightPath[64];
  snprintf(lightPath, sizeof(lightPath), "%s/light.theme", CONFIG_THEMES_DIR);
  if (!SdMan.exists(lightPath)) {
    saveToFile(lightPath, BUILTIN_LIGHT_THEME);
  }

  // Create dark.theme if it doesn't exist
  char darkPath[64];
  snprintf(darkPath, sizeof(darkPath), "%s/dark.theme", CONFIG_THEMES_DIR);
  if (!SdMan.exists(darkPath)) {
    saveToFile(darkPath, BUILTIN_DARK_THEME);
  }
}
