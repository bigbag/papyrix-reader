#include "test_utils.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// Mock dependencies
#include "HardwareSerial.h"

// Provide strcasecmp for portability
#ifdef _WIN32
#define strcasecmp _stricmp
#endif

// Minimal IniParser implementation for testing (avoiding file I/O dependencies)
class IniParser {
 public:
  using Callback = std::function<bool(const char* section, const char* key, const char* value)>;

  static bool parseString(const char* content, Callback callback) {
    if (!content) return false;

    char currentSection[64] = "";
    char line[256];
    const char* ptr = content;

    while (*ptr) {
      size_t len = 0;
      while (*ptr && *ptr != '\n' && len < sizeof(line) - 1) {
        if (*ptr != '\r') {
          line[len++] = *ptr;
        }
        ptr++;
      }
      line[len] = '\0';

      if (len == sizeof(line) - 1) {
        while (*ptr && *ptr != '\n') ptr++;
      }

      if (*ptr == '\n') ptr++;

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

      if (!parseLine(line, currentSection, callback)) {
        return true;
      }
    }

    return true;
  }

  static bool parseBool(const char* value, bool defaultValue = false) {
    if (!value || !*value) return defaultValue;

    if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0 ||
        strcmp(value, "1") == 0) {
      return true;
    }

    if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0 ||
        strcmp(value, "0") == 0) {
      return false;
    }

    return defaultValue;
  }

  static int parseInt(const char* value, int defaultValue = 0) {
    if (!value || !*value) return defaultValue;

    char* end;
    long result = strtol(value, &end, 10);
    if (end == value) return defaultValue;

    return static_cast<int>(result);
  }

  static uint8_t parseColor(const char* value, uint8_t defaultValue = 0xFF) {
    if (!value || !*value) return defaultValue;

    if (strcasecmp(value, "black") == 0) return 0x00;
    if (strcasecmp(value, "white") == 0) return 0xFF;

    int num = parseInt(value, -1);
    if (num >= 0 && num <= 255) {
      return static_cast<uint8_t>(num);
    }

    return defaultValue;
  }

 private:
  static void trimWhitespace(char* str) {
    if (!str || !*str) return;

    char* start = str;
    while (isspace(static_cast<unsigned char>(*start))) start++;

    if (!*start) {
      *str = '\0';
      return;
    }

    char* end = start + strlen(start) - 1;
    while (end > start && isspace(static_cast<unsigned char>(*end))) end--;
    *(end + 1) = '\0';

    if (start != str) {
      memmove(str, start, strlen(start) + 1);
    }
  }

  static bool parseLine(char* line, const char* currentSection, const Callback& callback) {
    if (line[0] == '#' || line[0] == ';' || line[0] == '\0') {
      return true;
    }

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
};

// Helper to collect parsed results
struct ParsedItem {
  std::string section;
  std::string key;
  std::string value;
};

int main() {
  TestUtils::TestRunner runner("IniParser");

  // ============================================
  // parseString() tests
  // ============================================

  // Test 1: Simple key-value without section
  {
    std::vector<ParsedItem> items;
    const char* ini = "key = value";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return true;
    });

    runner.expectEq(static_cast<size_t>(1), items.size(), "parseString: one key-value pair");
    if (!items.empty()) {
      runner.expectEqual("", items[0].section, "parseString: empty section before first [section]");
      runner.expectEqual("key", items[0].key, "parseString: key is 'key'");
      runner.expectEqual("value", items[0].value, "parseString: value is 'value'");
    }
  }

  // Test 2: Section header
  {
    std::vector<ParsedItem> items;
    const char* ini = "[section]\nkey = value";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return true;
    });

    runner.expectEq(static_cast<size_t>(1), items.size(), "parseString: one item with section");
    if (!items.empty()) {
      runner.expectEqual("section", items[0].section, "parseString: section is 'section'");
      runner.expectEqual("key", items[0].key, "parseString: key is 'key'");
    }
  }

  // Test 3: Multiple sections
  {
    std::vector<ParsedItem> items;
    const char* ini = "[first]\na = 1\n[second]\nb = 2";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return true;
    });

    runner.expectEq(static_cast<size_t>(2), items.size(), "parseString: two items in two sections");
    if (items.size() >= 2) {
      runner.expectEqual("first", items[0].section, "parseString: first section");
      runner.expectEqual("second", items[1].section, "parseString: second section");
    }
  }

  // Test 4: Comment with #
  {
    std::vector<ParsedItem> items;
    const char* ini = "# comment\nkey = value";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return true;
    });

    runner.expectEq(static_cast<size_t>(1), items.size(), "parseString: # comment ignored");
    if (!items.empty()) {
      runner.expectEqual("key", items[0].key, "parseString: key after # comment");
    }
  }

  // Test 5: Comment with ;
  {
    std::vector<ParsedItem> items;
    const char* ini = "; comment\nkey = value";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return true;
    });

    runner.expectEq(static_cast<size_t>(1), items.size(), "parseString: ; comment ignored");
  }

  // Test 6: Whitespace handling
  {
    std::vector<ParsedItem> items;
    const char* ini = "  key  =  value  ";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return true;
    });

    runner.expectEq(static_cast<size_t>(1), items.size(), "parseString: whitespace trimmed");
    if (!items.empty()) {
      runner.expectEqual("key", items[0].key, "parseString: key trimmed");
      runner.expectEqual("value", items[0].value, "parseString: value trimmed");
    }
  }

  // Test 7: Empty lines ignored
  {
    std::vector<ParsedItem> items;
    const char* ini = "\n\nkey = value\n\n";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return true;
    });

    runner.expectEq(static_cast<size_t>(1), items.size(), "parseString: empty lines ignored");
  }

  // Test 8: Missing equals sign (line ignored)
  {
    std::vector<ParsedItem> items;
    const char* ini = "no equals sign\nkey = value";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return true;
    });

    runner.expectEq(static_cast<size_t>(1), items.size(), "parseString: line without = ignored");
    if (!items.empty()) {
      runner.expectEqual("key", items[0].key, "parseString: valid line after invalid");
    }
  }

  // Test 9: Value with embedded equals
  {
    std::vector<ParsedItem> items;
    const char* ini = "equation = 1+1=2";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return true;
    });

    runner.expectEq(static_cast<size_t>(1), items.size(), "parseString: embedded = in value");
    if (!items.empty()) {
      runner.expectEqual("equation", items[0].key, "parseString: key with embedded = in value");
      runner.expectEqual("1+1=2", items[0].value, "parseString: value contains =");
    }
  }

  // Test 10: Callback returning false stops parsing
  {
    std::vector<ParsedItem> items;
    const char* ini = "a = 1\nb = 2\nc = 3";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return items.size() < 2;  // Stop after 2nd item
    });

    runner.expectEq(static_cast<size_t>(2), items.size(), "parseString: callback false stops parsing");
  }

  // Test 11: Empty section name
  {
    std::vector<ParsedItem> items;
    const char* ini = "[]\nkey = value";
    IniParser::parseString(ini, [&](const char* section, const char* key, const char* value) {
      items.push_back({section, key, value});
      return true;
    });

    runner.expectEq(static_cast<size_t>(1), items.size(), "parseString: empty section name");
    if (!items.empty()) {
      runner.expectEqual("", items[0].section, "parseString: empty section parsed as empty string");
    }
  }

  // Test 12: Null content
  {
    bool result = IniParser::parseString(nullptr, [](const char*, const char*, const char*) { return true; });
    runner.expectFalse(result, "parseString: null content returns false");
  }

  // ============================================
  // parseBool() tests
  // ============================================

  // Test 13: true values
  runner.expectTrue(IniParser::parseBool("true"), "parseBool: 'true' is true");
  runner.expectTrue(IniParser::parseBool("TRUE"), "parseBool: 'TRUE' is true (case insensitive)");
  runner.expectTrue(IniParser::parseBool("True"), "parseBool: 'True' is true");
  runner.expectTrue(IniParser::parseBool("yes"), "parseBool: 'yes' is true");
  runner.expectTrue(IniParser::parseBool("YES"), "parseBool: 'YES' is true");
  runner.expectTrue(IniParser::parseBool("on"), "parseBool: 'on' is true");
  runner.expectTrue(IniParser::parseBool("ON"), "parseBool: 'ON' is true");
  runner.expectTrue(IniParser::parseBool("1"), "parseBool: '1' is true");

  // Test 14: false values
  runner.expectFalse(IniParser::parseBool("false"), "parseBool: 'false' is false");
  runner.expectFalse(IniParser::parseBool("FALSE"), "parseBool: 'FALSE' is false");
  runner.expectFalse(IniParser::parseBool("no"), "parseBool: 'no' is false");
  runner.expectFalse(IniParser::parseBool("NO"), "parseBool: 'NO' is false");
  runner.expectFalse(IniParser::parseBool("off"), "parseBool: 'off' is false");
  runner.expectFalse(IniParser::parseBool("OFF"), "parseBool: 'OFF' is false");
  runner.expectFalse(IniParser::parseBool("0"), "parseBool: '0' is false");

  // Test 15: Invalid values use default
  runner.expectFalse(IniParser::parseBool("invalid", false), "parseBool: invalid uses default false");
  runner.expectTrue(IniParser::parseBool("invalid", true), "parseBool: invalid uses default true");
  runner.expectFalse(IniParser::parseBool("", false), "parseBool: empty uses default false");
  runner.expectTrue(IniParser::parseBool("", true), "parseBool: empty uses default true");
  runner.expectFalse(IniParser::parseBool(nullptr, false), "parseBool: null uses default false");

  // ============================================
  // parseInt() tests
  // ============================================

  // Test 16: Valid integers
  runner.expectEq(0, IniParser::parseInt("0"), "parseInt: '0'");
  runner.expectEq(42, IniParser::parseInt("42"), "parseInt: '42'");
  runner.expectEq(-10, IniParser::parseInt("-10"), "parseInt: '-10'");
  runner.expectEq(12345, IniParser::parseInt("12345"), "parseInt: '12345'");

  // Test 17: Invalid integers use default
  runner.expectEq(0, IniParser::parseInt(""), "parseInt: empty uses default 0");
  runner.expectEq(99, IniParser::parseInt("", 99), "parseInt: empty uses custom default");
  runner.expectEq(0, IniParser::parseInt("abc"), "parseInt: 'abc' uses default");
  runner.expectEq(0, IniParser::parseInt(nullptr), "parseInt: null uses default");

  // Test 18: Partial parse (stops at non-digit)
  runner.expectEq(123, IniParser::parseInt("123abc"), "parseInt: '123abc' parses 123");

  // ============================================
  // parseColor() tests
  // ============================================

  // Test 19: Named colors
  runner.expectEq(static_cast<uint8_t>(0x00), IniParser::parseColor("black"), "parseColor: 'black' is 0x00");
  runner.expectEq(static_cast<uint8_t>(0x00), IniParser::parseColor("BLACK"), "parseColor: 'BLACK' is 0x00");
  runner.expectEq(static_cast<uint8_t>(0xFF), IniParser::parseColor("white"), "parseColor: 'white' is 0xFF");
  runner.expectEq(static_cast<uint8_t>(0xFF), IniParser::parseColor("WHITE"), "parseColor: 'WHITE' is 0xFF");

  // Test 20: Numeric colors
  runner.expectEq(static_cast<uint8_t>(0), IniParser::parseColor("0"), "parseColor: '0' is 0");
  runner.expectEq(static_cast<uint8_t>(128), IniParser::parseColor("128"), "parseColor: '128' is 128");
  runner.expectEq(static_cast<uint8_t>(255), IniParser::parseColor("255"), "parseColor: '255' is 255");

  // Test 21: Out of range uses default
  runner.expectEq(static_cast<uint8_t>(0xFF), IniParser::parseColor("256"), "parseColor: '256' uses default");
  runner.expectEq(static_cast<uint8_t>(0xFF), IniParser::parseColor("-1"), "parseColor: '-1' uses default");
  runner.expectEq(static_cast<uint8_t>(0x80), IniParser::parseColor("invalid", 0x80),
                  "parseColor: invalid uses custom default");

  // Test 22: Empty and null
  runner.expectEq(static_cast<uint8_t>(0xFF), IniParser::parseColor(""), "parseColor: empty uses default");
  runner.expectEq(static_cast<uint8_t>(0xFF), IniParser::parseColor(nullptr), "parseColor: null uses default");

  return runner.allPassed() ? 0 : 1;
}
