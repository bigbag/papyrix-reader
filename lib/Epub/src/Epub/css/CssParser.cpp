#include "CssParser.h"

#include <Logging.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <esp_heap_caps.h>
#include <uzlib.h>

#define TAG "CSS"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {
constexpr uint32_t kCacheMagic = 0x53435850;  // PXCS
constexpr uint8_t kCacheVersion = 1;
constexpr size_t kStyleBytes = 47;
constexpr size_t kRuleAllocation = sizeof(std::pair<const std::string, CssStyle>) + 128;
constexpr CssLength CssStyle::* kLengths[] = {&CssStyle::marginTop,   &CssStyle::marginBottom, &CssStyle::marginLeft,
                                              &CssStyle::marginRight, &CssStyle::paddingTop,   &CssStyle::paddingBottom,
                                              &CssStyle::paddingLeft, &CssStyle::paddingRight};
static_assert(sizeof(float) == 4);

bool hasHeap(size_t bytes, size_t copies = 1) {
  // Include allocator overhead and all live declaration copies.
  bytes = (bytes + 128) * copies;
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= bytes &&
         heap_caps_get_free_size(MALLOC_CAP_8BIT) >= bytes;
}

bool appendCharacter(std::string& text, char c) {
  if (text.size() == text.capacity()) {
    const size_t capacity = std::max<size_t>(text.capacity() * 2, 16);
    if (!hasHeap(capacity + 1)) return false;
    text.reserve(capacity);
  }
  text += c;
  return true;
}

void encodeStyle(const CssStyle& style, uint8_t* bytes) {
  const auto& d = style.defined;
  const uint16_t flags = d.textAlign | d.fontStyle << 1 | d.fontWeight << 2 | d.direction << 3 | d.marginTop << 4 |
                         d.marginBottom << 5 | d.marginLeft << 6 | d.marginRight << 7 | d.paddingTop << 8 |
                         d.paddingBottom << 9 | d.paddingLeft << 10 | d.paddingRight << 11 | d.display << 12;
  std::memcpy(bytes, &flags, sizeof(flags));
  bytes[2] = static_cast<uint8_t>(style.textAlign);
  bytes[3] = static_cast<uint8_t>(style.fontStyle);
  bytes[4] = static_cast<uint8_t>(style.fontWeight);
  bytes[5] = static_cast<uint8_t>(style.direction);
  bytes[6] = static_cast<uint8_t>(style.display);
  for (size_t i = 0; i < 8; ++i) {
    const auto& length = style.*kLengths[i];
    std::memcpy(bytes + 7 + i * 5, &length.value, sizeof(length.value));
    bytes[11 + i * 5] = static_cast<uint8_t>(length.unit);
  }
}

bool decodeStyle(const uint8_t* bytes, CssStyle& style) {
  uint16_t flags;
  std::memcpy(&flags, bytes, sizeof(flags));
  if (!flags || (flags & ~0x1fff) || bytes[2] > static_cast<uint8_t>(TextAlign::Justify) ||
      bytes[3] > static_cast<uint8_t>(CssFontStyle::Initial) ||
      bytes[4] > static_cast<uint8_t>(CssFontWeight::Initial) || bytes[5] > static_cast<uint8_t>(TextDirection::Rtl) ||
      bytes[6] > static_cast<uint8_t>(CssDisplay::None))
    return false;
  style.textAlign = static_cast<TextAlign>(bytes[2]);
  style.fontStyle = static_cast<CssFontStyle>(bytes[3]);
  style.fontWeight = static_cast<CssFontWeight>(bytes[4]);
  style.direction = static_cast<TextDirection>(bytes[5]);
  style.display = static_cast<CssDisplay>(bytes[6]);
  auto& d = style.defined;
  d.textAlign = flags & 1;
  d.fontStyle = (flags >> 1) & 1;
  d.fontWeight = (flags >> 2) & 1;
  d.direction = (flags >> 3) & 1;
  d.marginTop = (flags >> 4) & 1;
  d.marginBottom = (flags >> 5) & 1;
  d.marginLeft = (flags >> 6) & 1;
  d.marginRight = (flags >> 7) & 1;
  d.paddingTop = (flags >> 8) & 1;
  d.paddingBottom = (flags >> 9) & 1;
  d.paddingLeft = (flags >> 10) & 1;
  d.paddingRight = (flags >> 11) & 1;
  d.display = (flags >> 12) & 1;
  for (size_t i = 0; i < 8; ++i) {
    auto& length = style.*kLengths[i];
    std::memcpy(&length.value, bytes + 7 + i * 5, sizeof(length.value));
    if (!std::isfinite(length.value) || bytes[11 + i * 5] > static_cast<uint8_t>(CssUnit::Percent)) return false;
    length.unit = static_cast<CssUnit>(bytes[11 + i * 5]);
  }
  return true;
}

std::string trim(const std::string& str) {
  size_t start = 0;
  while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
    ++start;
  }
  if (start == str.size()) return "";

  size_t end = str.size() - 1;
  while (end > start && std::isspace(static_cast<unsigned char>(str[end]))) {
    --end;
  }
  return str.substr(start, end - start + 1);
}

std::string toLower(const std::string& str) {
  std::string result = str;
  for (char& c : result) {
    if (c >= 'A' && c <= 'Z') {
      c = c - 'A' + 'a';
    }
  }
  return result;
}

// Split a whitespace-separated string into up to maxParts parts
int splitWhitespace(const std::string& str, std::string parts[], int maxParts) {
  int count = 0;
  size_t i = 0;
  while (i < str.size() && count < maxParts) {
    while (i < str.size() && std::isspace(static_cast<unsigned char>(str[i]))) ++i;
    if (i >= str.size()) break;
    size_t start = i;
    while (i < str.size() && !std::isspace(static_cast<unsigned char>(str[i]))) ++i;
    parts[count++] = str.substr(start, i - start);
  }
  return count;
}

bool isValidFontWeight(const std::string& v) {
  if (v == "normal" || v == "bold" || v == "bolder" || v == "lighter" || v == "inherit" || v == "initial") {
    return true;
  }
  char* endPtr = nullptr;
  const long weight = std::strtol(v.c_str(), &endPtr, 10);
  return endPtr != v.c_str() && *endPtr == '\0' && weight >= 100 && weight <= 900;
}

}  // namespace

CssParser::CssParser() {}

CssParser::~CssParser() {}

bool CssParser::parseFile(const char* filepath, const std::function<bool()>& shouldAbort) {
  FsFile file;
  auto fail = [&]() {
    file.close();
    clear();
    return false;
  };
  if (shouldAbort && shouldAbort()) return fail();
  if (!SdMan.openFileForRead("CSS", filepath, file)) {
    LOG_ERR(TAG, "Failed to open %s", filepath);
    return fail();
  }

  const size_t fileSize = file.size();
  if (fileSize > MAX_CSS_FILE_SIZE) {
    LOG_ERR(TAG, "File too large (%zu bytes): %s", fileSize, filepath);
    return fail();
  }

  std::string selector;
  std::string properties;
  bool inComment = false;
  bool inAtRule = false;
  bool inRule = false;
  bool inString = false;
  char stringQuote = 0;
  int braceCount = 0;

  int pushback = -1;
  size_t bytesRead = 0;
  auto readCharacter = [&]() {
    const int value = file.read();
    if (value >= 0) ++bytesRead;
    return value;
  };
  while (bytesRead < fileSize || pushback != -1) {
    if ((bytesRead & 255) == 0 && shouldAbort && shouldAbort()) return fail();
    char c;
    if (pushback != -1) {
      c = static_cast<char>(pushback);
      pushback = -1;
    } else {
      const int value = readCharacter();
      if (value < 0) return fail();
      c = static_cast<char>(value);
    }

    // Handle comment start '/*'
    if (!inComment && c == '/') {
      int next = -1;
      if (bytesRead < fileSize) {
        next = readCharacter();
        if (next < 0) return fail();
      }
      if (next == '*') {
        inComment = true;
        continue;
      }
      if (next != -1) pushback = next;
    }

    if (inComment) {
      if (c == '*') {
        int next = -1;
        if (bytesRead < fileSize) {
          next = readCharacter();
          if (next < 0) return fail();
        }
        if (next == '/') {
          inComment = false;
        } else if (next != -1) {
          pushback = next;
        }
      }
      continue;
    }

    // Ignore carriage returns
    if (c == '\r') continue;

    if (!inRule) {
      // Handle AT-rules
      if (inAtRule) {
        if (c == '{') {
          braceCount++;
        } else if (c == '}') {
          if (braceCount > 0) {
            braceCount--;
            if (braceCount == 0) {
              inAtRule = false;
            }
          }
        } else if (c == ';' && braceCount == 0) {
          inAtRule = false;
        }
        continue;
      }

      if (c == '@') {
        inAtRule = true;
        braceCount = 0;
        continue;
      }

      if (c == '{') {
        inRule = true;
        braceCount = 1;
        if (!hasHeap(selector.size() + 1)) return fail();
        selector = trim(selector);
        properties.clear();
        continue;
      }

      if (selector.size() < MAX_CSS_SELECTOR_LENGTH) {
        if (!appendCharacter(selector, c)) return fail();
      }
    } else {
      // Inside declaration block
      if (!inString && (c == '"' || c == '\'')) {
        inString = true;
        stringQuote = c;
        if (!appendCharacter(properties, c)) return fail();
        continue;
      } else if (inString && c == stringQuote) {
        inString = false;
        stringQuote = 0;
        if (!appendCharacter(properties, c)) return fail();
        continue;
      }

      if (!inString) {
        if (c == '{') {
          braceCount++;
          if (!appendCharacter(properties, c)) return fail();
          continue;
        } else if (c == '}') {
          braceCount--;
          if (braceCount == 0) {
            if (!selector.empty() && !properties.empty()) {
              if (!parseRule(selector, properties, shouldAbort)) return fail();
            }
            selector.clear();
            properties.clear();
            inRule = false;
            continue;
          }
        }
      }

      if (!appendCharacter(properties, c)) return fail();
    }
  }

  // Handle incomplete rule at EOF
  if (inRule && !properties.empty()) {
    if (!selector.empty()) {
      if (!parseRule(selector, properties, shouldAbort)) return fail();
    }
  }

  if (shouldAbort && shouldAbort()) return fail();
  if (styleMap_.size() >= MAX_CSS_RULES) {
    LOG_DBG(TAG, "Rule limit reached (%zu max)", MAX_CSS_RULES);
  }
  file.close();
  LOG_INF(TAG, "Loaded %d style rules from %s", static_cast<int>(styleMap_.size()), filepath);
  return true;
}

bool CssParser::loadCache(const char* path, uint32_t sourceFingerprint, const std::function<bool()>& shouldAbort) {
  clear();
  FsFile file;
  auto fail = [&]() {
    file.close();
    clear();
    return false;
  };
  if (!sourceFingerprint || (shouldAbort && shouldAbort()) || !SdMan.exists(path) ||
      !SdMan.openFileForRead(TAG, path, file))
    return fail();
  const size_t size = file.size();
  if (size < 15 || size > 15 + MAX_CSS_RULES * (4 + MAX_CSS_SELECTOR_LENGTH + kStyleBytes)) return fail();
  uint32_t crc = 0xffffffff;
  auto read = [&](auto& value) {
    if (!serialization::readPodChecked(file, value)) return false;
    crc = uzlib_crc32(&value, sizeof(value), crc);
    return true;
  };
  uint32_t magic, fingerprint;
  uint8_t version;
  uint16_t count;
  if (!read(magic) || magic != kCacheMagic || !read(version) || version != kCacheVersion || !read(fingerprint) ||
      fingerprint != sourceFingerprint || !read(count) || count > MAX_CSS_RULES)
    return fail();
  for (uint16_t i = 0; i < count; ++i) {
    if (shouldAbort && shouldAbort()) return fail();
    uint32_t length;
    if (!read(length) || !length || length > MAX_CSS_SELECTOR_LENGTH ||
        size - file.position() < length + kStyleBytes + 4 || !hasHeap(length + 1))
      return fail();
    std::string selector(length, '\0');
    if (file.read(selector.data(), length) != static_cast<int>(length)) return fail();
    crc = uzlib_crc32(selector.data(), length, crc);
    uint8_t bytes[kStyleBytes];
    CssStyle style;
    if (!read(bytes) || !decodeStyle(bytes, style) || selector.find('\0') != std::string::npos ||
        styleMap_.find(selector) != styleMap_.end() || !hasHeap(kRuleAllocation))
      return fail();
    styleMap_.emplace(std::move(selector), style);
  }
  uint32_t storedCrc;
  const bool ok = serialization::readPodChecked(file, storedCrc) && storedCrc == (crc ^ 0xffffffff) &&
                  file.position() == size && !(shouldAbort && shouldAbort());
  if (!ok) return fail();
  file.close();
  return true;
}

bool CssParser::saveCache(const char* path, uint32_t sourceFingerprint,
                          const std::function<bool()>& shouldAbort) const {
  if (!sourceFingerprint || (shouldAbort && shouldAbort()) || !hasHeap(std::strlen(path) + 5, 2)) return false;
  const std::string temporary = std::string(path) + ".tmp";
  FsFile file;
  auto fail = [&]() {
    file.close();
    SdMan.remove(temporary.c_str());
    return false;
  };
  if (!SdMan.openFileForWrite(TAG, temporary, file)) return fail();
  uint32_t crc = 0xffffffff;
  auto write = [&](const auto& value) {
    if (!serialization::writePodChecked(file, value)) return false;
    crc = uzlib_crc32(&value, sizeof(value), crc);
    return true;
  };
  const uint16_t count = styleMap_.size();
  if (!write(kCacheMagic) || !write(kCacheVersion) || !write(sourceFingerprint) || !write(count)) return fail();
  for (const auto& entry : styleMap_) {
    if (shouldAbort && shouldAbort()) return fail();
    const uint32_t length = entry.first.size();
    if (!write(length) || file.write(reinterpret_cast<const uint8_t*>(entry.first.data()), length) != length)
      return fail();
    crc = uzlib_crc32(entry.first.data(), length, crc);
    uint8_t bytes[kStyleBytes];
    encodeStyle(entry.second, bytes);
    if (!write(bytes)) return fail();
  }
  if (!serialization::writePodChecked(file, crc ^ 0xffffffff) || !file.sync() || (shouldAbort && shouldAbort()))
    return fail();
  if (!file.close()) return fail();
  if (!SdMan.commitFile(temporary.c_str(), path)) return fail();
  return true;
}

const CssStyle* CssParser::getStyleForClass(const std::string& className) const {
  auto it = styleMap_.find(className);
  if (it != styleMap_.end()) {
    return &it->second;
  }
  return nullptr;
}

CssStyle CssParser::getTagStyle(const std::string& tagName) const {
  CssStyle combined;
  const CssStyle* style = getStyleForClass(tagName);
  if (style) {
    combined.applyOver(*style);
  }
  return combined;
}

CssStyle CssParser::getCombinedStyle(const std::string& tagName, const std::string& classNames) const {
  CssStyle combined;

  // First apply tag-level styles
  const CssStyle* tagStyle = getStyleForClass(tagName);
  if (tagStyle) {
    combined.applyOver(*tagStyle);
  }

  // Split class names by whitespace and apply each
  size_t start = 0;
  size_t len = classNames.length();

  while (start < len) {
    // Skip whitespace
    while (start < len && std::isspace(static_cast<unsigned char>(classNames[start]))) {
      ++start;
    }
    if (start >= len) break;

    // Find end of class name
    size_t end = start;
    while (end < len && !std::isspace(static_cast<unsigned char>(classNames[end]))) {
      ++end;
    }

    if (end > start) {
      std::string className = classNames.substr(start, end - start);

      // Try class-only selector (.classname)
      const CssStyle* classOnly = getStyleForClass("." + className);
      if (classOnly) {
        combined.applyOver(*classOnly);
      }

      // Try tag.class selector (p.classname)
      const CssStyle* tagAndClass = getStyleForClass(tagName + "." + className);
      if (tagAndClass) {
        combined.applyOver(*tagAndClass);
      }
    }

    start = end;
  }

  return combined;
}

bool CssParser::parseRule(const std::string& selector, const std::string& properties,
                          const std::function<bool()>& shouldAbort) {
  // Handle comma-separated selectors
  size_t start = 0;
  size_t len = selector.length();

  while (start < len) {
    size_t end = selector.find(',', start);
    if (end == std::string::npos) end = len;

    if ((shouldAbort && shouldAbort()) || !hasHeap(end - start + 1, 2)) return false;
    std::string singleSelector = trim(selector.substr(start, end - start));

    // Only accept tag, .class, and tag.class. Skip combinators (+>~), descendant
    // (whitespace inside trimmed selector), attribute ([), pseudo (:), ID (#),
    // and wildcard (*) — the matcher can't apply them, and keeping them would
    // fill MAX_CSS_RULES with rules that never match.
    const bool supportedSelector =
        !singleSelector.empty() && singleSelector.find_first_of("+>[:#~* ") == std::string::npos;

    if (supportedSelector) {
      CssStyle style;

      // Split properties by semicolon
      size_t propStart = 0;
      size_t propLen = properties.length();

      while (propStart < propLen) {
        size_t propEnd = properties.find(';', propStart);
        if (propEnd == std::string::npos) propEnd = propLen;

        if (shouldAbort && shouldAbort()) return false;
        size_t trimmedEnd = propEnd;
        while (propStart < trimmedEnd && std::isspace(static_cast<unsigned char>(properties[propStart]))) ++propStart;
        while (trimmedEnd > propStart && std::isspace(static_cast<unsigned char>(properties[trimmedEnd - 1])))
          --trimmedEnd;

        if (propStart < trimmedEnd) {
          if (!hasHeap(trimmedEnd - propStart + 1, 8)) return false;
          std::string prop = properties.substr(propStart, trimmedEnd - propStart);
          size_t colonPos = prop.find(':');
          if (colonPos != std::string::npos && colonPos > 0) {
            std::string propName = trim(prop.substr(0, colonPos));
            std::string propValue = trim(prop.substr(colonPos + 1));
            propName = toLower(propName);
            parseProperty(propName, propValue, style);
          }
        }

        propStart = propEnd + 1;
      }

      if (style.defined.anySet()) {
        for (auto member : kLengths) {
          if (!std::isfinite((style.*member).value)) return false;
        }
        auto it = styleMap_.find(singleSelector);
        if (it != styleMap_.end()) {
          it->second.applyOver(style);
        } else if (styleMap_.size() < MAX_CSS_RULES) {
          if (!hasHeap(kRuleAllocation)) return false;
          styleMap_.emplace(std::move(singleSelector), style);
        }
      }
    }

    start = end + 1;
  }
  return true;
}

void CssParser::parseProperty(const std::string& name, const std::string& rawValue, CssStyle& style) {
  std::string value = toLower(trim(rawValue));
  size_t imp = value.find("!important");
  if (imp != std::string::npos) value = trim(value.substr(0, imp));

  if (name == "text-align") {
    std::string v = toLower(trim(value));
    if (v != "inherit") {
      style.textAlign = parseTextAlign(v);
      style.defined.textAlign = 1;
    }
  } else if (name == "font-style") {
    // Only valid keywords mark the property defined. Invalid values are ignored.
    if (value == "italic" || value == "oblique" || value == "normal" || value == "inherit" || value == "initial") {
      style.fontStyle = parseFontStyle(value);
      style.defined.fontStyle = 1;
    }
  } else if (name == "font-weight") {
    if (isValidFontWeight(value)) {
      style.fontWeight = parseFontWeight(value);
      style.defined.fontWeight = 1;
    }
  } else if (name == "direction") {
    std::string v = toLower(trim(value));
    if (v == "rtl") {
      style.direction = TextDirection::Rtl;
      style.defined.direction = 1;
    } else if (v == "ltr") {
      style.direction = TextDirection::Ltr;
      style.defined.direction = 1;
    }
  } else if (name == "margin-top") {
    style.marginTop = parseCssLength(value);
    style.defined.marginTop = 1;
  } else if (name == "margin-bottom") {
    style.marginBottom = parseCssLength(value);
    style.defined.marginBottom = 1;
  } else if (name == "margin-left") {
    style.marginLeft = parseCssLength(value);
    style.defined.marginLeft = 1;
  } else if (name == "margin-right") {
    style.marginRight = parseCssLength(value);
    style.defined.marginRight = 1;
  } else if (name == "margin") {
    parseMarginShorthand(value, style);
  } else if (name == "padding-top") {
    style.paddingTop = parseCssLength(value);
    style.defined.paddingTop = 1;
  } else if (name == "padding-bottom") {
    style.paddingBottom = parseCssLength(value);
    style.defined.paddingBottom = 1;
  } else if (name == "padding-left") {
    style.paddingLeft = parseCssLength(value);
    style.defined.paddingLeft = 1;
  } else if (name == "padding-right") {
    style.paddingRight = parseCssLength(value);
    style.defined.paddingRight = 1;
  } else if (name == "padding") {
    parsePaddingShorthand(value, style);
  } else if (name == "display") {
    std::string v = toLower(trim(value));
    style.display = (v == "none") ? CssDisplay::None : CssDisplay::Block;
    style.defined.display = 1;
  }
}

TextAlign CssParser::parseTextAlign(const std::string& value) {
  std::string v = toLower(trim(value));

  if (v == "left" || v == "start") {
    return TextAlign::Left;
  } else if (v == "right" || v == "end") {
    return TextAlign::Right;
  } else if (v == "center") {
    return TextAlign::Center;
  } else if (v == "justify") {
    return TextAlign::Justify;
  }

  return TextAlign::Left;
}

CssFontStyle CssParser::parseFontStyle(const std::string& value) {
  std::string v = toLower(trim(value));

  if (v == "italic" || v == "oblique") {
    return CssFontStyle::Italic;
  } else if (v == "inherit") {
    return CssFontStyle::Inherit;
  } else if (v == "initial") {
    return CssFontStyle::Initial;
  }

  return CssFontStyle::Normal;
}

CssFontWeight CssParser::parseFontWeight(const std::string& value) {
  std::string v = toLower(trim(value));

  if (v == "bold" || v == "bolder" || v == "700" || v == "800" || v == "900") {
    return CssFontWeight::Bold;
  } else if (v == "inherit") {
    return CssFontWeight::Inherit;
  } else if (v == "initial") {
    return CssFontWeight::Initial;
  }

  return CssFontWeight::Normal;
}

CssLength CssParser::parseCssLength(const std::string& value) {
  std::string v = toLower(trim(value));
  if (v.empty() || v == "auto" || v == "inherit" || v == "initial") {
    return CssLength{};
  }

  char* endPtr = nullptr;
  float numVal = std::strtof(v.c_str(), &endPtr);
  if (endPtr == v.c_str()) {
    return CssLength{};
  }

  std::string unitStr = trim(std::string(endPtr));

  if (unitStr == "em") {
    return CssLength{numVal, CssUnit::Em};
  } else if (unitStr == "rem") {
    return CssLength{numVal, CssUnit::Rem};
  } else if (unitStr == "pt") {
    return CssLength{numVal, CssUnit::Points};
  } else if (unitStr == "%") {
    return CssLength{numVal, CssUnit::Percent};
  }

  return CssLength{numVal, CssUnit::Pixels};
}

void CssParser::parseMarginShorthand(const std::string& value, CssStyle& style) {
  std::string parts[4];
  int count = splitWhitespace(toLower(trim(value)), parts, 4);
  if (count == 0) return;

  CssLength values[4];
  for (int i = 0; i < count; ++i) {
    values[i] = parseCssLength(parts[i]);
  }

  // CSS margin shorthand: 1=all, 2=TB LR, 3=T LR B, 4=T R B L
  switch (count) {
    case 1:
      style.marginTop = style.marginRight = style.marginBottom = style.marginLeft = values[0];
      break;
    case 2:
      style.marginTop = style.marginBottom = values[0];
      style.marginRight = style.marginLeft = values[1];
      break;
    case 3:
      style.marginTop = values[0];
      style.marginRight = style.marginLeft = values[1];
      style.marginBottom = values[2];
      break;
    default:
      style.marginTop = values[0];
      style.marginRight = values[1];
      style.marginBottom = values[2];
      style.marginLeft = values[3];
      break;
  }
  style.defined.marginTop = style.defined.marginRight = 1;
  style.defined.marginBottom = style.defined.marginLeft = 1;
}

void CssParser::parsePaddingShorthand(const std::string& value, CssStyle& style) {
  std::string parts[4];
  int count = splitWhitespace(toLower(trim(value)), parts, 4);
  if (count == 0) return;

  CssLength values[4];
  for (int i = 0; i < count; ++i) {
    values[i] = parseCssLength(parts[i]);
  }

  switch (count) {
    case 1:
      style.paddingTop = style.paddingRight = style.paddingBottom = style.paddingLeft = values[0];
      break;
    case 2:
      style.paddingTop = style.paddingBottom = values[0];
      style.paddingRight = style.paddingLeft = values[1];
      break;
    case 3:
      style.paddingTop = values[0];
      style.paddingRight = style.paddingLeft = values[1];
      style.paddingBottom = values[2];
      break;
    default:
      style.paddingTop = values[0];
      style.paddingRight = values[1];
      style.paddingBottom = values[2];
      style.paddingLeft = values[3];
      break;
  }
  style.defined.paddingTop = style.defined.paddingRight = 1;
  style.defined.paddingBottom = style.defined.paddingLeft = 1;
}

CssStyle CssParser::parseInlineStyle(const std::string& styleAttr) {
  CssStyle style;

  if (styleAttr.empty()) {
    return style;
  }

  size_t propStart = 0;
  size_t propLen = styleAttr.length();

  while (propStart < propLen) {
    size_t propEnd = styleAttr.find(';', propStart);
    if (propEnd == std::string::npos) propEnd = propLen;

    std::string prop = trim(styleAttr.substr(propStart, propEnd - propStart));

    if (!prop.empty()) {
      size_t colonPos = prop.find(':');
      if (colonPos != std::string::npos && colonPos > 0) {
        std::string propName = trim(prop.substr(0, colonPos));
        std::string propValue = trim(prop.substr(colonPos + 1));
        propName = toLower(propName);
        parseProperty(propName, propValue, style);
      }
    }

    propStart = propEnd + 1;
  }

  return style;
}
