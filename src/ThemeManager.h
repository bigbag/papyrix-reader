#pragma once

#include <string>
#include <vector>

#include "Theme.h"

/**
 * Singleton manager for theme loading and application.
 *
 * Loads themes from /themes/*.theme files on SD card.
 * Falls back to builtin themes when files are missing.
 *
 * Usage:
 *   THEME_MANAGER.loadTheme("dark");
 *   renderer.fillRect(x, y, w, h, THEME.selectionFillBlack);
 */
class ThemeManager {
 public:
  static ThemeManager& instance();

  /**
   * Load a theme by name.
   * Looks for /themes/<name>.theme on SD card.
   * Falls back to builtin theme if file not found.
   *
   * @param themeName Name of the theme (without .theme extension)
   * @return true if loaded from file, false if using builtin fallback
   */
  bool loadTheme(const char* themeName);

  /**
   * Save current theme to file.
   * @param themeName Name for the theme file
   * @return true if saved successfully
   */
  bool saveTheme(const char* themeName);

  /**
   * Get the currently active theme.
   */
  const Theme& current() const { return activeTheme; }

  /**
   * Get mutable reference to current theme for modifications.
   */
  Theme& mutableCurrent() { return activeTheme; }

  /**
   * Apply builtin light theme.
   */
  void applyLightTheme();

  /**
   * Apply builtin dark theme.
   */
  void applyDarkTheme();

  /**
   * List available theme files on SD card.
   * @return Vector of theme names (without .theme extension)
   */
  std::vector<std::string> listAvailableThemes();

  /**
   * Create default theme files on SD card if they don't exist.
   * Called during boot to give users template files to edit.
   */
  void createDefaultThemeFiles();

  /**
   * Get the current theme name.
   */
  const char* currentThemeName() const { return themeName; }

 private:
  ThemeManager();
  ~ThemeManager() = default;
  ThemeManager(const ThemeManager&) = delete;
  ThemeManager& operator=(const ThemeManager&) = delete;

  bool loadFromFile(const char* path);
  bool saveToFile(const char* path, const Theme& theme);

  Theme activeTheme;
  char themeName[32];
};

// Convenience macros
#define THEME_MANAGER ThemeManager::instance()
#define THEME ThemeManager::instance().current()
