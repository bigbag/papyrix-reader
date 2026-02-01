#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Include the C markdown parser header
extern "C" {
#include "md_parser.h"
}

// Test helper: collect tokens from parsing
struct TokenCollector {
  std::vector<md_token_type_t> types;
  std::vector<std::string> texts;
  std::vector<uint8_t> data;

  void clear() {
    types.clear();
    texts.clear();
    data.clear();
  }
};

static bool collectTokens(const md_token_t* token, void* userData) {
  TokenCollector* collector = static_cast<TokenCollector*>(userData);
  collector->types.push_back(token->type);
  if (token->text && token->length > 0) {
    collector->texts.push_back(std::string(token->text, token->length));
  } else {
    collector->texts.push_back("");
  }
  collector->data.push_back(token->data);
  return true;
}

// Helper: check if token sequence contains specific type
static bool hasTokenType(const TokenCollector& c, md_token_type_t type) {
  for (auto t : c.types) {
    if (t == type) return true;
  }
  return false;
}

// Helper: get text for first token of given type
static std::string getFirstTextOfType(const TokenCollector& c, md_token_type_t type) {
  for (size_t i = 0; i < c.types.size(); i++) {
    if (c.types[i] == type) return c.texts[i];
  }
  return "";
}

// Helper: count tokens of given type
static int countTokenType(const TokenCollector& c, md_token_type_t type) {
  int count = 0;
  for (auto t : c.types) {
    if (t == type) count++;
  }
  return count;
}

int main() {
  TestUtils::TestRunner runner("Markdown Parser (md_parser)");

  TokenCollector collector;
  md_parser_t parser;

  // ============================================
  // Basic text parsing
  // ============================================

  // Test 1: Plain text
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "Hello world", 11);

    runner.expectTrue(hasTokenType(collector, MD_TEXT), "Plain text: has TEXT token");
    std::string combined;
    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_TEXT) combined += collector.texts[i];
    }
    runner.expectEqual("Hello world", combined, "Plain text: content matches");
  }

  // Test 2: Empty string
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "", 0);

    runner.expectTrue(collector.types.empty(), "Empty string: no tokens");
  }

  // ============================================
  // Headers
  // ============================================

  // Test 3: H1 header
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "# Title", 7);

    runner.expectTrue(hasTokenType(collector, MD_HEADER_START), "H1: has HEADER_START");
    runner.expectTrue(hasTokenType(collector, MD_HEADER_END), "H1: has HEADER_END");

    // Find header level
    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_HEADER_START) {
        runner.expectEq(static_cast<uint8_t>(1), collector.data[i], "H1: level is 1");
        break;
      }
    }
  }

  // Test 4: H3 header
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "### Subtitle", 12);

    runner.expectTrue(hasTokenType(collector, MD_HEADER_START), "H3: has HEADER_START");
    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_HEADER_START) {
        runner.expectEq(static_cast<uint8_t>(3), collector.data[i], "H3: level is 3");
        break;
      }
    }
  }

  // Test 5: H6 header (max level)
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "###### Deep", 11);

    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_HEADER_START) {
        runner.expectEq(static_cast<uint8_t>(6), collector.data[i], "H6: level is 6");
        break;
      }
    }
  }

  // Test 6: 7 hashes is not a header (too many)
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "####### TooMany", 15);

    runner.expectFalse(hasTokenType(collector, MD_HEADER_START), "7 hashes: not a header");
  }

  // ============================================
  // Bold formatting
  // ============================================

  // Test 7: Bold with **
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "**bold**", 8);

    runner.expectTrue(hasTokenType(collector, MD_BOLD_START), "Bold **: has BOLD_START");
    runner.expectTrue(hasTokenType(collector, MD_BOLD_END), "Bold **: has BOLD_END");
  }

  // Test 8: Bold with __
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "__bold__", 8);

    runner.expectTrue(hasTokenType(collector, MD_BOLD_START), "Bold __: has BOLD_START");
    runner.expectTrue(hasTokenType(collector, MD_BOLD_END), "Bold __: has BOLD_END");
  }

  // Test 9: Unclosed bold
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "**unclosed", 10);

    runner.expectTrue(hasTokenType(collector, MD_BOLD_START), "Unclosed bold: has BOLD_START");
    runner.expectTrue(hasTokenType(collector, MD_BOLD_END), "Unclosed bold: auto-closed at end");
  }

  // ============================================
  // Italic formatting
  // ============================================

  // Test 10: Italic with *
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "*italic*", 8);

    runner.expectTrue(hasTokenType(collector, MD_ITALIC_START), "Italic *: has ITALIC_START");
    runner.expectTrue(hasTokenType(collector, MD_ITALIC_END), "Italic *: has ITALIC_END");
  }

  // Test 11: Italic with _
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "_italic_", 8);

    runner.expectTrue(hasTokenType(collector, MD_ITALIC_START), "Italic _: has ITALIC_START");
    runner.expectTrue(hasTokenType(collector, MD_ITALIC_END), "Italic _: has ITALIC_END");
  }

  // ============================================
  // Bold + Italic combined
  // ============================================

  // Test 12: Bold and italic
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "***both***", 10);

    int boldCount = countTokenType(collector, MD_BOLD_START);
    int italicCount = countTokenType(collector, MD_ITALIC_START);
    runner.expectTrue(boldCount > 0 || italicCount > 0, "Bold+Italic: has formatting");
  }

  // ============================================
  // Inline code
  // ============================================

  // Test 13: Inline code
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "`code`", 6);

    runner.expectTrue(hasTokenType(collector, MD_CODE_INLINE), "Inline code: has CODE_INLINE");
    std::string code = getFirstTextOfType(collector, MD_CODE_INLINE);
    runner.expectEqual("code", code, "Inline code: content is 'code'");
  }

  // Test 14: Inline code with spaces
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "`foo bar`", 9);

    std::string code = getFirstTextOfType(collector, MD_CODE_INLINE);
    runner.expectEqual("foo bar", code, "Inline code: preserves spaces");
  }

  // Test 15: Unclosed backtick (not code)
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "`unclosed", 9);

    runner.expectFalse(hasTokenType(collector, MD_CODE_INLINE), "Unclosed backtick: no CODE_INLINE");
  }

  // ============================================
  // Code blocks
  // ============================================

  // Test 16: Code block
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    const char* input = "```\ncode\n```";
    md_parse(&parser, input, strlen(input));

    runner.expectTrue(hasTokenType(collector, MD_CODE_BLOCK_START), "Code block: has start");
    runner.expectTrue(hasTokenType(collector, MD_CODE_BLOCK_END), "Code block: has end");
  }

  // Test 17: Code block with language
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    const char* input = "```python\nprint('hi')\n```";
    md_parse(&parser, input, strlen(input));

    std::string lang = getFirstTextOfType(collector, MD_CODE_BLOCK_START);
    runner.expectEqual("python", lang, "Code block: language is 'python'");
  }

  // ============================================
  // Links
  // ============================================

  // Test 18: Basic link
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "[text](url)", 11);

    runner.expectTrue(hasTokenType(collector, MD_LINK_TEXT_START), "Link: has text start");
    runner.expectTrue(hasTokenType(collector, MD_LINK_URL), "Link: has URL");
    std::string url = getFirstTextOfType(collector, MD_LINK_URL);
    runner.expectEqual("url", url, "Link: URL is 'url'");
  }

  // Test 19: Link with full URL
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    const char* input = "[Google](https://google.com)";
    md_parse(&parser, input, strlen(input));

    std::string url = getFirstTextOfType(collector, MD_LINK_URL);
    runner.expectEqual("https://google.com", url, "Link: full URL preserved");
  }

  // ============================================
  // Images
  // ============================================

  // Test 20: Basic image
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "![alt](img.jpg)", 15);

    runner.expectTrue(hasTokenType(collector, MD_IMAGE_ALT_START), "Image: has alt start");
    runner.expectTrue(hasTokenType(collector, MD_IMAGE_URL), "Image: has URL");
    std::string url = getFirstTextOfType(collector, MD_IMAGE_URL);
    runner.expectEqual("img.jpg", url, "Image: URL is 'img.jpg'");
  }

  // ============================================
  // Lists
  // ============================================

  // Test 21: Unordered list with -
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "- item", 6);

    runner.expectTrue(hasTokenType(collector, MD_LIST_ITEM_START), "Unordered list -: has item");
    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_LIST_ITEM_START) {
        runner.expectEq(static_cast<uint8_t>(0), collector.data[i], "Unordered: data is 0");
        break;
      }
    }
  }

  // Test 22: Unordered list with *
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "* item", 6);

    runner.expectTrue(hasTokenType(collector, MD_LIST_ITEM_START), "Unordered list *: has item");
  }

  // Test 23: Ordered list
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "1. first", 8);

    runner.expectTrue(hasTokenType(collector, MD_LIST_ITEM_START), "Ordered list: has item");
    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_LIST_ITEM_START) {
        runner.expectEq(static_cast<uint8_t>(1), collector.data[i], "Ordered: data is 1");
        break;
      }
    }
  }

  // Test 24: Ordered list with larger number
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "42. item", 8);

    for (size_t i = 0; i < collector.types.size(); i++) {
      if (collector.types[i] == MD_LIST_ITEM_START) {
        runner.expectEq(static_cast<uint8_t>(42), collector.data[i], "Ordered 42: data is 42");
        break;
      }
    }
  }

  // ============================================
  // Blockquotes
  // ============================================

  // Test 25: Basic blockquote
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "> quote", 7);

    runner.expectTrue(hasTokenType(collector, MD_BLOCKQUOTE_START), "Blockquote: has start");
  }

  // ============================================
  // Horizontal rules
  // ============================================

  // Test 26: HR with ---
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "---", 3);

    runner.expectTrue(hasTokenType(collector, MD_HR), "HR ---: has HR token");
  }

  // Test 27: HR with ***
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "***", 3);

    runner.expectTrue(hasTokenType(collector, MD_HR), "HR ***: has HR token");
  }

  // Test 28: HR with ___
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "___", 3);

    runner.expectTrue(hasTokenType(collector, MD_HR), "HR ___: has HR token");
  }

  // Test 29: HR with spaces
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "- - -", 5);

    runner.expectTrue(hasTokenType(collector, MD_HR), "HR - - -: has HR token");
  }

  // ============================================
  // Strikethrough
  // ============================================

  // Test 30: Strikethrough
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "~~struck~~", 10);

    runner.expectTrue(hasTokenType(collector, MD_STRIKE_START), "Strikethrough: has start");
    runner.expectTrue(hasTokenType(collector, MD_STRIKE_END), "Strikethrough: has end");
  }

  // ============================================
  // Escape sequences
  // ============================================

  // Test 31: Escaped asterisk
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "\\*not italic\\*", 14);

    runner.expectFalse(hasTokenType(collector, MD_ITALIC_START), "Escaped *: no italic");
    runner.expectTrue(hasTokenType(collector, MD_TEXT), "Escaped *: has text");
  }

  // Test 32: Escaped backtick
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "\\`not code\\`", 12);

    runner.expectFalse(hasTokenType(collector, MD_CODE_INLINE), "Escaped `: no code");
  }

  // ============================================
  // Newlines
  // ============================================

  // Test 33: Newline token
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "line1\nline2", 11);

    runner.expectTrue(hasTokenType(collector, MD_NEWLINE), "Newline: has NEWLINE token");
  }

  // ============================================
  // Feature flags
  // ============================================

  // Test 34: Disabled headers feature
  {
    collector.clear();
    md_config_t config = {
        .callback = collectTokens,
        .user_data = &collector,
        .features = MD_FEAT_ALL & ~MD_FEAT_HEADERS  // Disable headers
    };
    md_parser_init_ex(&parser, &config);
    md_parse(&parser, "# Title", 7);

    runner.expectFalse(hasTokenType(collector, MD_HEADER_START), "Disabled headers: no HEADER_START");
  }

  // Test 35: Basic features only
  {
    collector.clear();
    md_config_t config = {
        .callback = collectTokens,
        .user_data = &collector,
        .features = MD_FEAT_BASIC  // Only headers, bold, italic, inline code
    };
    md_parser_init_ex(&parser, &config);
    md_parse(&parser, "- list item", 11);

    runner.expectFalse(hasTokenType(collector, MD_LIST_ITEM_START), "Basic features: no lists");
  }

  // ============================================
  // Parser reset
  // ============================================

  // Test 36: Parser reset
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    md_parse(&parser, "**bold", 6);  // Unclosed bold

    collector.clear();
    md_parser_reset(&parser);
    md_parse(&parser, "normal", 6);

    runner.expectFalse(hasTokenType(collector, MD_BOLD_START), "Reset: no lingering bold");
    runner.expectFalse(hasTokenType(collector, MD_BOLD_END), "Reset: no bold end");
  }

  // ============================================
  // Token name utility
  // ============================================

  // Test 37: Token names
  {
    runner.expectEqual("TEXT", std::string(md_token_name(MD_TEXT)), "Token name: TEXT");
    runner.expectEqual("HEADER_START", std::string(md_token_name(MD_HEADER_START)), "Token name: HEADER_START");
    runner.expectEqual("BOLD_START", std::string(md_token_name(MD_BOLD_START)), "Token name: BOLD_START");
    runner.expectEqual("HR", std::string(md_token_name(MD_HR)), "Token name: HR");
  }

  // Test 38: Unknown token type
  {
    const char* name = md_token_name(static_cast<md_token_type_t>(999));
    runner.expectEqual("UNKNOWN", std::string(name), "Token name: unknown returns UNKNOWN");
  }

  // ============================================
  // Edge cases
  // ============================================

  // Test 39: Multiple paragraphs
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    const char* input = "para1\n\npara2";
    md_parse(&parser, input, strlen(input));

    int newlineCount = countTokenType(collector, MD_NEWLINE);
    runner.expectTrue(newlineCount >= 2, "Multiple paragraphs: has newlines");
  }

  // Test 40: Mixed formatting
  {
    collector.clear();
    md_parser_init(&parser, collectTokens, &collector);
    const char* input = "**bold** and *italic* and `code`";
    md_parse(&parser, input, strlen(input));

    runner.expectTrue(hasTokenType(collector, MD_BOLD_START), "Mixed: has bold");
    runner.expectTrue(hasTokenType(collector, MD_ITALIC_START), "Mixed: has italic");
    runner.expectTrue(hasTokenType(collector, MD_CODE_INLINE), "Mixed: has code");
  }

  return runner.allPassed() ? 0 : 1;
}
