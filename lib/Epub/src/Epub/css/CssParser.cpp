#include "CssParser.h"

#include <Logging.h>
#include <SDCardManager.h>

#define TAG "CSS"

#include <cctype>
#include <cstdlib>

namespace {

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

}  // namespace

CssParser::CssParser() {}

CssParser::~CssParser() {}

bool CssParser::parseFile(const char* filepath) {
  FsFile file;
  if (!SdMan.openFileForRead("CSS", filepath, file)) {
    LOG_ERR(TAG, "Failed to open %s", filepath);
    return false;
  }

  const size_t fileSize = file.size();
  if (fileSize > MAX_CSS_FILE_SIZE) {
    LOG_ERR(TAG, "File too large (%zu bytes): %s", fileSize, filepath);
    file.close();
    return false;
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
  while (file.available() || pushback != -1) {
    char c;
    if (pushback != -1) {
      c = static_cast<char>(pushback);
      pushback = -1;
    } else {
      c = static_cast<char>(file.read());
    }

    // Handle comment start '/*'
    if (!inComment && c == '/') {
      int next = -1;
      if (file.available()) next = file.read();
      if (next == '*') {
        inComment = true;
        continue;
      }
      if (next != -1) pushback = next;
    }

    if (inComment) {
      if (c == '*') {
        int next = -1;
        if (file.available()) next = file.read();
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
        selector = trim(selector);
        properties.clear();
        continue;
      }

      if (selector.size() < MAX_CSS_SELECTOR_LENGTH) {
        selector += c;
      }
    } else {
      // Inside declaration block
      if (!inString && (c == '"' || c == '\'')) {
        inString = true;
        stringQuote = c;
        properties += c;
        continue;
      } else if (inString && c == stringQuote) {
        inString = false;
        stringQuote = 0;
        properties += c;
        continue;
      }

      if (!inString) {
        if (c == '{') {
          braceCount++;
          properties += c;
          continue;
        } else if (c == '}') {
          braceCount--;
          if (braceCount == 0) {
            properties = trim(properties);
            if (!selector.empty() && !properties.empty()) {
              parseRule(selector, properties);
            }
            selector.clear();
            properties.clear();
            inRule = false;
            continue;
          }
        }
      }

      properties += c;
    }
  }

  // Handle incomplete rule at EOF
  if (inRule && !properties.empty()) {
    properties = trim(properties);
    selector = trim(selector);
    if (!selector.empty()) {
      parseRule(selector, properties);
    }
  }

  if (styleMap_.size() >= MAX_CSS_RULES) {
    LOG_DBG(TAG, "Rule limit reached (%zu max)", MAX_CSS_RULES);
  }
  file.close();
  LOG_INF(TAG, "Loaded %d style rules from %s", static_cast<int>(styleMap_.size()), filepath);
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

void CssParser::parseRule(const std::string& selector, const std::string& properties) {
  // Handle comma-separated selectors
  size_t start = 0;
  size_t len = selector.length();

  while (start < len) {
    size_t end = selector.find(',', start);
    if (end == std::string::npos) end = len;

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

        std::string prop = trim(properties.substr(propStart, propEnd - propStart));

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

      if (style.defined.anySet()) {
        auto it = styleMap_.find(singleSelector);
        if (it != styleMap_.end()) {
          it->second.applyOver(style);
        } else if (styleMap_.size() < MAX_CSS_RULES) {
          styleMap_[singleSelector] = style;
        }
      }
    }

    start = end + 1;
  }
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
    style.fontStyle = parseFontStyle(value);
    style.defined.fontStyle = 1;
  } else if (name == "font-weight") {
    style.fontWeight = parseFontWeight(value);
    style.defined.fontWeight = 1;
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
  }

  return CssFontStyle::Normal;
}

CssFontWeight CssParser::parseFontWeight(const std::string& value) {
  std::string v = toLower(trim(value));

  if (v == "bold" || v == "bolder" || v == "700" || v == "800" || v == "900") {
    return CssFontWeight::Bold;
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
