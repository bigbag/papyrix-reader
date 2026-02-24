#pragma once

#include <cstdint>
#include <functional>

/**
 * Simple INI file parser for theme configuration.
 *
 * Parses INI format:
 *   [section]
 *   key = value
 *   # comment
 *
 * Designed for minimal memory usage on ESP32-C3.
 */
class IniParser {
 public:
  /**
   * Callback for each key-value pair found.
   * @param section Current section name (empty if before first section)
   * @param key The key name
   * @param value The value (trimmed of whitespace)
   * @return true to continue parsing, false to stop
   */
  using Callback = std::function<bool(const char* section, const char* key, const char* value)>;

  /**
   * Parse an INI file from SD card.
   * @param path Path to the INI file
   * @param callback Function called for each key-value pair
   * @return true if file was parsed successfully, false on error
   */
  static bool parseFile(const char* path, Callback callback);

  /**
   * Parse an INI string in memory.
   * @param content The INI content string
   * @param callback Function called for each key-value pair
   * @return true if parsed successfully
   */
  static bool parseString(const char* content, Callback callback);

  /**
   * Helper to parse boolean values.
   * Accepts: true/false, yes/no, 1/0, on/off
   */
  static bool parseBool(const char* value, bool defaultValue = false);

  /**
   * Helper to parse integer values.
   */
  static int parseInt(const char* value, int defaultValue = 0);

  /**
   * Helper to parse color values.
   * Accepts: black/white, or 0/255
   */
  static uint8_t parseColor(const char* value, uint8_t defaultValue = 0xFF);

 private:
  static void trimWhitespace(char* str);
  static bool parseLine(char* line, const char* currentSection, const Callback& callback);
};
