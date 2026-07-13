#pragma once

#include <map>
#include <string>
#include <vector>

#include "CssStyle.h"

/**
 * CssParser - Simple CSS parser for extracting supported properties
 *
 * Handles:
 * - Class selectors (.classname)
 * - Element.class selectors (p.classname)
 * - Tag selectors (p, div, etc.)
 * - Multiple selectors separated by commas
 * - Inline styles
 *
 * Limitations:
 * - Does not support complex selectors (descendant, child, etc.)
 * - Does not support pseudo-classes or pseudo-elements
 * - Only extracts properties we actually use
 */
class CssParser {
 public:
  CssParser();
  ~CssParser();

  /**
   * Parse a CSS file and add its rules to the style map
   * Returns true if parsing was successful
   */
  bool parseFile(const char* filepath);

  /**
   * Get the style for a given selector (class or tag)
   * Returns nullptr if no style is defined
   */
  const CssStyle* getStyleForClass(const std::string& className) const;

  /**
   * Get the style for a tag name (e.g., "p", "div")
   */
  CssStyle getTagStyle(const std::string& tagName) const;

  /**
   * Get the combined style for a tag with multiple class names (space-separated)
   * Styles are merged in order, later classes override earlier ones
   */
  CssStyle getCombinedStyle(const std::string& tagName, const std::string& classNames) const;

  /**
   * Parse an inline style attribute (e.g., "text-align: center; font-weight: bold;")
   * Returns a CssStyle with the parsed properties
   * Static method - can be called without a CssParser instance
   */
  static CssStyle parseInlineStyle(const std::string& styleAttr);

  bool hasStyles() const { return !styleMap_.empty(); }
  size_t getStyleCount() const { return styleMap_.size(); }
  void clear() { styleMap_.clear(); }

 private:
  void parseRule(const std::string& selector, const std::string& properties);
  static void parseProperty(const std::string& name, const std::string& value, CssStyle& style);
  static TextAlign parseTextAlign(const std::string& value);
  static CssFontStyle parseFontStyle(const std::string& value);
  static CssFontWeight parseFontWeight(const std::string& value);
  static CssLength parseCssLength(const std::string& value);
  static void parseMarginShorthand(const std::string& value, CssStyle& style);
  static void parsePaddingShorthand(const std::string& value, CssStyle& style);

  static constexpr size_t MAX_CSS_RULES = 512;
  static constexpr size_t MAX_CSS_SELECTOR_LENGTH = 256;
  static constexpr size_t MAX_CSS_FILE_SIZE = 64 * 1024;

  std::map<std::string, CssStyle> styleMap_;
};
