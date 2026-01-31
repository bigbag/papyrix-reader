#include "IniParser.h"

#include <SDCardManager.h>

#include <cctype>
#include <cstring>

bool IniParser::parseFile(const char* path, Callback callback) {
  FsFile file = SdMan.open(path, O_RDONLY);
  if (!file) {
    return false;
  }

  char currentSection[64] = "";
  char line[256];

  while (file.available()) {
    // Read line
    size_t len = 0;
    while (file.available() && len < sizeof(line) - 1) {
      int c = file.read();
      if (c < 0 || c == '\n') break;
      if (c != '\r') {
        line[len++] = static_cast<char>(c);
      }
    }
    line[len] = '\0';

    // Discard remainder of long lines
    if (len == sizeof(line) - 1) {
      while (file.available()) {
        int c = file.read();
        if (c < 0 || c == '\n') break;
      }
    }

    // Check if this is a section header
    trimWhitespace(line);
    if (line[0] == '[') {
      char* end = strchr(line, ']');
      if (end) {
        *end = '\0';
        strncpy(currentSection, line + 1, sizeof(currentSection) - 1);
        currentSection[sizeof(currentSection) - 1] = '\0';
      }
      continue;
    }

    // Parse key=value
    if (!parseLine(line, currentSection, callback)) {
      file.close();
      return true;  // Callback requested stop
    }
  }

  file.close();
  return true;
}

bool IniParser::parseString(const char* content, Callback callback) {
  if (!content) return false;

  char currentSection[64] = "";
  char line[256];
  const char* ptr = content;

  while (*ptr) {
    // Read line
    size_t len = 0;
    while (*ptr && *ptr != '\n' && len < sizeof(line) - 1) {
      if (*ptr != '\r') {
        line[len++] = *ptr;
      }
      ptr++;
    }
    line[len] = '\0';

    // Discard remainder of long lines
    if (len == sizeof(line) - 1) {
      while (*ptr && *ptr != '\n') ptr++;
    }

    if (*ptr == '\n') ptr++;

    // Check if this is a section header
    trimWhitespace(line);
    if (line[0] == '[') {
      char* end = strchr(line, ']');
      if (end) {
        *end = '\0';
        strncpy(currentSection, line + 1, sizeof(currentSection) - 1);
        currentSection[sizeof(currentSection) - 1] = '\0';
      }
      continue;
    }

    // Parse key=value
    if (!parseLine(line, currentSection, callback)) {
      return true;  // Callback requested stop
    }
  }

  return true;
}

bool IniParser::parseLine(char* line, const char* currentSection, const Callback& callback) {
  // Skip comments and empty lines
  if (line[0] == '#' || line[0] == ';' || line[0] == '\0') {
    return true;
  }

  // Find '='
  char* eq = strchr(line, '=');
  if (!eq) return true;

  *eq = '\0';
  char* key = line;
  char* value = eq + 1;

  trimWhitespace(key);
  trimWhitespace(value);

  if (key[0] == '\0') return true;

  return callback(currentSection, key, value);
}

void IniParser::trimWhitespace(char* str) {
  if (!str || !*str) return;

  // Trim leading
  char* start = str;
  while (isspace((unsigned char)*start)) start++;

  // Handle all-whitespace string
  if (!*start) {
    *str = '\0';
    return;
  }

  // Trim trailing
  char* end = start + strlen(start) - 1;
  while (end > start && isspace((unsigned char)*end)) end--;
  *(end + 1) = '\0';

  // Move to beginning if needed
  if (start != str) {
    memmove(str, start, strlen(start) + 1);
  }
}

bool IniParser::parseBool(const char* value, bool defaultValue) {
  if (!value || !*value) return defaultValue;

  // Check true values
  if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0 ||
      strcmp(value, "1") == 0) {
    return true;
  }

  // Check false values
  if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0 ||
      strcmp(value, "0") == 0) {
    return false;
  }

  return defaultValue;
}

int IniParser::parseInt(const char* value, int defaultValue) {
  if (!value || !*value) return defaultValue;

  char* end;
  long result = strtol(value, &end, 10);
  if (end == value) return defaultValue;  // No conversion

  return static_cast<int>(result);
}

uint8_t IniParser::parseColor(const char* value, uint8_t defaultValue) {
  if (!value || !*value) return defaultValue;

  if (strcasecmp(value, "black") == 0) return 0x00;
  if (strcasecmp(value, "white") == 0) return 0xFF;

  // Try numeric
  int num = parseInt(value, -1);
  if (num >= 0 && num <= 255) {
    return static_cast<uint8_t>(num);
  }

  return defaultValue;
}
