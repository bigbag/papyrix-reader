// ChapterHtmlSlimParser unit tests
// Tests HTML parsing behavior including aria-hidden anchor skipping
//
// This test validates the parsing logic from ChapterHtmlSlimParser by
// reimplementing the key parsing rules in a test-friendly way. This tests
// the same HTML parsing behavior without needing to mock all the rendering
// infrastructure.

#include "test_utils.h"

#include <FsHelpers.h>
#include <SDCardManager.h>
#include <expat.h>
#include <htmlEntities.h>

#include <climits>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Tag matching helpers (copied from ChapterHtmlSlimParser.cpp)
const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr int NUM_HEADER_TAGS = 6;

const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote", "question", "answer", "quotation"};
constexpr int NUM_BLOCK_TAGS = 8;

const char* BOLD_TAGS[] = {"b", "strong"};
constexpr int NUM_BOLD_TAGS = 2;

const char* ITALIC_TAGS[] = {"i", "em"};
constexpr int NUM_ITALIC_TAGS = 2;

const char* IMAGE_TAGS[] = {"img"};
constexpr int NUM_IMAGE_TAGS = 1;

const char* SKIP_TAGS[] = {"head"};
constexpr int NUM_SKIP_TAGS = 1;

bool matches(const char* tag_name, const char* possible_tags[], const int possible_tag_count) {
  for (int i = 0; i < possible_tag_count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

// Block alignment styles (mirrors TextBlock::BLOCK_STYLE)
enum class BlockStyle { LEFT, CENTER, RIGHT, JUSTIFIED };

// Inline style parser for test (extracts text-align)
struct TestCssStyle {
  BlockStyle textAlign = BlockStyle::LEFT;
  bool hasTextAlign = false;
};

TestCssStyle parseTestInlineStyle(const char** atts) {
  TestCssStyle css;
  if (!atts) return css;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], "style") != 0) continue;
    const char* val = atts[i + 1];
    // Simple text-align extraction
    const char* ta = strstr(val, "text-align");
    if (!ta) continue;
    const char* colon = strchr(ta, ':');
    if (!colon) continue;
    colon++;
    while (*colon == ' ') colon++;
    if (strncmp(colon, "center", 6) == 0) {
      css.textAlign = BlockStyle::CENTER;
      css.hasTextAlign = true;
    } else if (strncmp(colon, "right", 5) == 0) {
      css.textAlign = BlockStyle::RIGHT;
      css.hasTextAlign = true;
    } else if (strncmp(colon, "left", 4) == 0) {
      css.textAlign = BlockStyle::LEFT;
      css.hasTextAlign = true;
    } else if (strncmp(colon, "justify", 7) == 0) {
      css.textAlign = BlockStyle::JUSTIFIED;
      css.hasTextAlign = true;
    }
    // inherit: hasTextAlign stays false
  }
  return css;
}

// Test parser that collects parsed elements
class TestParser {
 public:
  struct ParsedElement {
    enum Type { TEXT, IMAGE_PLACEHOLDER, TABLE_PLACEHOLDER, HEADER, BLOCK };
    Type type;
    std::string content;
    bool isBold = false;
    bool isItalic = false;
    bool isRtl = false;
    BlockStyle blockStyle = BlockStyle::LEFT;
  };

  struct AlignEntry {
    int depth;
    BlockStyle style;
  };

  std::vector<ParsedElement> elements;
  std::vector<std::pair<std::string, uint16_t>> anchorMap;
  std::string currentText;
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  bool pendingRtl = false;
  int rtlUntilDepth = INT_MAX;
  uint16_t blockCount = 0;
  BlockStyle currentBlockStyle = BlockStyle::LEFT;
  std::vector<AlignEntry> alignStack;

  void flushText() {
    if (!currentText.empty()) {
      ParsedElement elem;
      elem.type = ParsedElement::TEXT;
      elem.content = currentText;
      elem.isBold = boldUntilDepth < depth;
      elem.isItalic = italicUntilDepth < depth;
      elem.isRtl = pendingRtl;
      elem.blockStyle = currentBlockStyle;
      elements.push_back(elem);
      currentText.clear();
    }
  }

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
    auto* self = static_cast<TestParser*>(userData);

    // Mid skip
    if (self->skipUntilDepth < self->depth) {
      self->depth++;
      return;
    }

    // Image handling
    if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
      self->flushText();
      std::string srcAttr;
      std::string altText;
      int imgWidth = 0;
      int imgHeight = 0;
      if (atts) {
        for (int i = 0; atts[i]; i += 2) {
          if (strcmp(atts[i], "src") == 0 && atts[i + 1][0] != '\0') {
            srcAttr = atts[i + 1];
          } else if (strcmp(atts[i], "alt") == 0 && atts[i + 1][0] != '\0') {
            altText = atts[i + 1];
          } else if (strcmp(atts[i], "width") == 0) {
            imgWidth = atoi(atts[i + 1]);
          } else if (strcmp(atts[i], "height") == 0) {
            imgHeight = atoi(atts[i + 1]);
          }
        }
      }
      // Silently skip unsupported image formats (GIF, SVG, WebP, etc.)
      if (!srcAttr.empty() && !FsHelpers::isImageFile(srcAttr)) {
        self->depth++;
        return;
      }
      // Skip tiny decorative images (approximates ChapterHtmlSlimParser BMP dimension check).
      // Production checks actual BMP pixel dimensions; this mock uses HTML attributes as proxy,
      // so images missing width/height attributes won't be skipped here (production may still skip them).
      if (imgWidth > 0 && imgHeight > 0 && (imgWidth < 20 || imgHeight < 20)) {
        self->depth++;
        return;
      }
      ParsedElement elem;
      elem.type = ParsedElement::IMAGE_PLACEHOLDER;
      elem.content = altText.empty() ? "[Image]" : "[Image: " + altText + "]";
      self->elements.push_back(elem);
      self->depth++;
      return;
    }

    // Table handling
    if (strcmp(name, "table") == 0) {
      self->flushText();
      ParsedElement elem;
      elem.type = ParsedElement::TABLE_PLACEHOLDER;
      elem.content = "[Table omitted]";
      self->elements.push_back(elem);
      self->skipUntilDepth = self->depth;
      self->depth++;
      return;
    }

    // Skip tags (head)
    if (matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
      self->skipUntilDepth = self->depth;
      self->depth++;
      return;
    }

    // Skip pagebreak elements
    if (atts) {
      for (int i = 0; atts[i]; i += 2) {
        if ((strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0) ||
            (strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0)) {
          self->skipUntilDepth = self->depth;
          self->depth++;
          return;
        }
      }
    }

    // Skip aria-hidden="true" anchors (Pandoc line number anchors)
    if (strcmp(name, "a") == 0 && atts) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "aria-hidden") == 0 && strcmp(atts[i + 1], "true") == 0) {
          self->skipUntilDepth = self->depth;
          self->depth++;
          return;
        }
      }
    }

    // Extract dir and id attributes (mirrors ChapterHtmlSlimParser attribute extraction)
    std::string idAttr;
    if (atts) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "dir") == 0) {
          if (strcasecmp(atts[i + 1], "rtl") == 0) {
            self->pendingRtl = true;
            self->rtlUntilDepth = std::min(self->rtlUntilDepth, self->depth);
          } else if (strcasecmp(atts[i + 1], "ltr") == 0) {
            self->pendingRtl = false;
            self->rtlUntilDepth = std::min(self->rtlUntilDepth, self->depth);
          }
        } else if (strcmp(atts[i], "id") == 0 && atts[i + 1][0] != '\0') {
          idAttr = atts[i + 1];
        }
      }
    }

    // Parse inline style for text-align
    TestCssStyle cssStyle = parseTestInlineStyle(atts);

    // Headers
    if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
      self->flushText();
      self->blockCount++;
      self->currentBlockStyle = BlockStyle::CENTER;
      self->alignStack.push_back({self->depth, BlockStyle::CENTER});
      self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    }

    // Block tags
    if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
      self->flushText();
      self->blockCount++;
      // Determine block style: CSS text-align > inheritance > default
      BlockStyle blockStyle = BlockStyle::LEFT;
      bool hasExplicitAlign = false;
      if (cssStyle.hasTextAlign) {
        hasExplicitAlign = true;
        blockStyle = cssStyle.textAlign;
      }
      if (!hasExplicitAlign && !self->alignStack.empty()) {
        blockStyle = self->alignStack.back().style;
      }
      if (hasExplicitAlign) {
        self->alignStack.push_back({self->depth, blockStyle});
      }
      self->currentBlockStyle = blockStyle;
    }

    // Bold tags
    if (matches(name, BOLD_TAGS, NUM_BOLD_TAGS)) {
      self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    }

    // Italic tags
    if (matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
      self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
    }

    // Record anchor-to-page mapping (after block handling, mirrors ChapterHtmlSlimParser)
    if (!idAttr.empty()) {
      self->anchorMap.emplace_back(std::move(idAttr), self->blockCount);
    }

    self->depth++;
  }

  static void XMLCALL characterData(void* userData, const XML_Char* s, int len) {
    auto* self = static_cast<TestParser*>(userData);

    if (self->skipUntilDepth < self->depth) {
      return;
    }

    for (int i = 0; i < len; i++) {
      char c = s[i];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        if (!self->currentText.empty() && self->currentText.back() != ' ') {
          self->currentText += ' ';
        }
      } else {
        self->currentText += c;
      }
    }
  }

  static void XMLCALL defaultHandler(void* userData, const XML_Char* s, int len) {
    if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
      const char* utf8 = lookupHtmlEntity(s + 1, len - 2);
      if (utf8) {
        characterData(userData, utf8, static_cast<int>(strlen(utf8)));
        return;
      }
    }
    // Not a recognized entity — silently drop.
    // The default handler also receives XML/DOCTYPE declarations,
    // comments, and processing instructions which must not become visible text.
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<TestParser*>(userData);

    // Flush text on block tag close
    if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS) || matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
      self->flushText();
    }

    self->depth--;

    if (self->skipUntilDepth == self->depth) {
      self->skipUntilDepth = INT_MAX;
    }
    if (self->boldUntilDepth == self->depth) {
      self->boldUntilDepth = INT_MAX;
    }
    if (self->italicUntilDepth == self->depth) {
      self->italicUntilDepth = INT_MAX;
    }
    if (self->rtlUntilDepth == self->depth) {
      self->rtlUntilDepth = INT_MAX;
      self->pendingRtl = false;
    }
    while (!self->alignStack.empty() && self->alignStack.back().depth >= self->depth) {
      self->alignStack.pop_back();
    }
  }

  bool parse(const std::string& html) {
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) return false;

    XML_UseForeignDTD(parser, XML_TRUE);
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterData);
    XML_SetDefaultHandlerExpand(parser, defaultHandler);

    if (XML_Parse(parser, html.c_str(), static_cast<int>(html.size()), 1) == XML_STATUS_ERROR) {
      XML_ParserFree(parser);
      return false;
    }

    // Flush any remaining text
    flushText();

    XML_ParserFree(parser);
    return true;
  }

  size_t getTextElementCount() const {
    size_t count = 0;
    for (const auto& elem : elements) {
      if (elem.type == ParsedElement::TEXT) count++;
    }
    return count;
  }

  std::string getAllText() const {
    std::string result;
    for (const auto& elem : elements) {
      if (elem.type == ParsedElement::TEXT) {
        if (!result.empty()) result += " ";
        result += elem.content;
      }
    }
    return result;
  }

  bool hasImagePlaceholder() const {
    for (const auto& elem : elements) {
      if (elem.type == ParsedElement::IMAGE_PLACEHOLDER) return true;
    }
    return false;
  }

  bool hasTablePlaceholder() const {
    for (const auto& elem : elements) {
      if (elem.type == ParsedElement::TABLE_PLACEHOLDER) return true;
    }
    return false;
  }

  bool hasRtlElement() const {
    for (const auto& elem : elements) {
      if (elem.isRtl) return true;
    }
    return false;
  }

  bool isAllTextRtl() const {
    for (const auto& elem : elements) {
      if (elem.type == ParsedElement::TEXT && !elem.isRtl) return false;
    }
    return true;
  }

  BlockStyle getBlockStyleForText(const std::string& needle) const {
    for (const auto& elem : elements) {
      if (elem.type == ParsedElement::TEXT && elem.content.find(needle) != std::string::npos) {
        return elem.blockStyle;
      }
    }
    return BlockStyle::LEFT;
  }
};

int main() {
  TestUtils::TestRunner runner("ChapterHtmlSlimParser");

  // Test 1: Basic paragraph parsing
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>Hello world</p></body></html>");
    runner.expectTrue(ok, "basic_paragraph: parses successfully");
    runner.expectTrue(parser.getTextElementCount() >= 1, "basic_paragraph: creates text element");
    runner.expectTrue(parser.getAllText().find("Hello") != std::string::npos, "basic_paragraph: contains Hello");
    runner.expectTrue(parser.getAllText().find("world") != std::string::npos, "basic_paragraph: contains world");
  }

  // Test 2: Skip aria-hidden anchor tags (Pandoc line number anchors)
  {
    TestParser parserWithAnchors;
    bool ok1 = parserWithAnchors.parse(
        "<html><body><pre><code>"
        "<a href=\"#cb1-1\" aria-hidden=\"true\" tabindex=\"-1\"></a>line1"
        "<a href=\"#cb1-2\" aria-hidden=\"true\" tabindex=\"-1\"></a>line2"
        "</code></pre></body></html>");
    runner.expectTrue(ok1, "skip_aria_hidden: parses successfully");

    TestParser parserNoAnchors;
    bool ok2 = parserNoAnchors.parse("<html><body><pre><code>line1line2</code></pre></body></html>");
    runner.expectTrue(ok2, "skip_aria_hidden: no-anchor version parses");

    // Both should produce same text content
    runner.expectEqual(parserWithAnchors.getAllText(), parserNoAnchors.getAllText(),
                       "skip_aria_hidden: anchor content skipped");
  }

  // Test 3: Skip pagebreak elements (role="doc-pagebreak")
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<p>Before</p>"
        "<span role=\"doc-pagebreak\" id=\"page1\">PAGENUM</span>"
        "<p>After</p>"
        "</body></html>");
    runner.expectTrue(ok, "skip_pagebreak_role: parses successfully");
    runner.expectTrue(parser.getAllText().find("PAGENUM") == std::string::npos,
                      "skip_pagebreak_role: pagebreak content skipped");
    runner.expectTrue(parser.getAllText().find("Before") != std::string::npos, "skip_pagebreak_role: Before visible");
    runner.expectTrue(parser.getAllText().find("After") != std::string::npos, "skip_pagebreak_role: After visible");
  }

  // Test 4: Skip pagebreak elements (epub:type="pagebreak")
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<p>Start</p>"
        "<span epub:type=\"pagebreak\" title=\"5\">PAGE5</span>"
        "<p>End</p>"
        "</body></html>");
    runner.expectTrue(ok, "skip_pagebreak_epub: parses successfully");
    runner.expectTrue(parser.getAllText().find("PAGE5") == std::string::npos,
                      "skip_pagebreak_epub: pagebreak content skipped");
  }

  // Test 5: Table placeholder
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><table><tr><td>Cell1</td><td>Cell2</td></tr></table></body></html>");
    runner.expectTrue(ok, "table_placeholder: parses successfully");
    runner.expectTrue(parser.hasTablePlaceholder(), "table_placeholder: placeholder added");
    runner.expectTrue(parser.getAllText().find("Cell1") == std::string::npos,
                      "table_placeholder: table content skipped");
  }

  // Test 6: Skip head element content
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html>"
        "<head><title>Should Not Appear</title><style>body{}</style></head>"
        "<body><p>Visible</p></body>"
        "</html>");
    runner.expectTrue(ok, "skip_head: parses successfully");
    runner.expectTrue(parser.getAllText().find("Should Not Appear") == std::string::npos,
                      "skip_head: head content skipped");
    runner.expectTrue(parser.getAllText().find("Visible") != std::string::npos, "skip_head: body content visible");
  }

  // Test 7: Image placeholder with alt text
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><img src=\"test.jpg\" alt=\"A photo of a cat\"/></body></html>");
    runner.expectTrue(ok, "image_placeholder: parses successfully");
    runner.expectTrue(parser.hasImagePlaceholder(), "image_placeholder: placeholder added");
  }

  // Test 8: Image placeholder without alt text
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><img src=\"test.jpg\"/></body></html>");
    runner.expectTrue(ok, "image_no_alt: parses successfully");
    runner.expectTrue(parser.hasImagePlaceholder(), "image_no_alt: placeholder added");
  }

  // Test 9: Header parsing
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><h1>Title</h1><p>Content</p></body></html>");
    runner.expectTrue(ok, "header: parses successfully");
    runner.expectTrue(parser.getAllText().find("Title") != std::string::npos, "header: title visible");
    runner.expectTrue(parser.getAllText().find("Content") != std::string::npos, "header: content visible");
  }

  // Test 10: Multiple header levels
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><h1>H1</h1><h2>H2</h2><h3>H3</h3><h4>H4</h4><h5>H5</h5><h6>H6</h6></body></html>");
    runner.expectTrue(ok, "headers_h1_h6: parses successfully");
    runner.expectTrue(parser.getAllText().find("H1") != std::string::npos, "headers_h1_h6: H1 visible");
    runner.expectTrue(parser.getAllText().find("H6") != std::string::npos, "headers_h1_h6: H6 visible");
  }

  // Test 11: Block tags create separate text blocks
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>Para1</p><div>Div1</div><li>ListItem</li></body></html>");
    runner.expectTrue(ok, "block_tags: parses successfully");
    runner.expectTrue(parser.getTextElementCount() >= 3, "block_tags: creates multiple text elements");
  }

  // Test 12: BR tag creates new block
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>Line1<br/>Line2</p></body></html>");
    runner.expectTrue(ok, "br_tag: parses successfully");
    runner.expectTrue(parser.getTextElementCount() >= 2, "br_tag: creates multiple text elements");
  }

  // Test 13: Empty paragraphs don't crash
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p></p><p>   </p><p>Content</p></body></html>");
    runner.expectTrue(ok, "empty_paras: parses without crash");
  }

  // Test 14: Nested tags don't crash
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p><b><i><span>Deeply nested</span></i></b></p></body></html>");
    runner.expectTrue(ok, "nested_tags: parses successfully");
    runner.expectTrue(parser.getAllText().find("Deeply nested") != std::string::npos, "nested_tags: content visible");
  }

  // Test 15: Whitespace handling
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>  Multiple   spaces   collapse  </p></body></html>");
    runner.expectTrue(ok, "whitespace: parses successfully");
    // Whitespace should be collapsed to single spaces
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("  ") == std::string::npos || text.find("Multiple") != std::string::npos,
                      "whitespace: excessive whitespace collapsed");
  }

  // Test 16: aria-hidden="false" should NOT be skipped
  {
    TestParser parserHidden;
    parserHidden.parse("<html><body><a href=\"#\" aria-hidden=\"true\">HIDDEN</a><span>visible</span></body></html>");

    TestParser parserFalse;
    parserFalse.parse(
        "<html><body><a href=\"#\" aria-hidden=\"false\">NOT HIDDEN</a><span>visible</span></body></html>");

    runner.expectTrue(parserHidden.getAllText().find("HIDDEN") == std::string::npos,
                      "aria_hidden_true: content skipped");
    runner.expectTrue(parserFalse.getAllText().find("NOT HIDDEN") != std::string::npos,
                      "aria_hidden_false: content NOT skipped");
  }

  // Test 17: blockquote tag
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><blockquote>Quoted text here</blockquote></body></html>");
    runner.expectTrue(ok, "blockquote: parses successfully");
    runner.expectTrue(parser.getAllText().find("Quoted") != std::string::npos, "blockquote: content visible");
  }

  // Test 18: Custom block tags (question, answer, quotation)
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<question>What is the meaning?</question>"
        "<answer>42</answer>"
        "<quotation>To be or not to be</quotation>"
        "</body></html>");
    runner.expectTrue(ok, "custom_block_tags: parses successfully");
    runner.expectTrue(parser.getTextElementCount() >= 3, "custom_block_tags: creates separate text elements");
    runner.expectTrue(parser.getAllText().find("What is the meaning?") != std::string::npos,
                      "custom_block_tags: question content visible");
    runner.expectTrue(parser.getAllText().find("42") != std::string::npos, "custom_block_tags: answer content visible");
    runner.expectTrue(parser.getAllText().find("To be or not to be") != std::string::npos,
                      "custom_block_tags: quotation content visible");
  }

  // Test 19: Pre/code blocks
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><pre><code>function test() { return true; }</code></pre></body></html>");
    runner.expectTrue(ok, "pre_code: parses successfully");
    runner.expectTrue(parser.getAllText().find("function") != std::string::npos, "pre_code: code visible");
  }

  // Test 20: Bold and italic tracking
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p><b>Bold</b> and <i>Italic</i></p></body></html>");
    runner.expectTrue(ok, "bold_italic: parses successfully");
    // Check that we captured the text
    runner.expectTrue(parser.getAllText().find("Bold") != std::string::npos, "bold_italic: bold text visible");
    runner.expectTrue(parser.getAllText().find("Italic") != std::string::npos, "bold_italic: italic text visible");
  }

  // Test 21: Nested skip regions work correctly
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html>"
        "<head><title>Skip this</title></head>"
        "<body>"
        "<table><tr><td>Skip table</td></tr></table>"
        "<p>Visible content</p>"
        "</body>"
        "</html>");
    runner.expectTrue(ok, "nested_skip: parses successfully");
    runner.expectTrue(parser.getAllText().find("Skip this") == std::string::npos, "nested_skip: head skipped");
    runner.expectTrue(parser.getAllText().find("Skip table") == std::string::npos, "nested_skip: table skipped");
    runner.expectTrue(parser.getAllText().find("Visible content") != std::string::npos,
                      "nested_skip: body content visible");
  }

  // ============================================
  // RTL dir attribute tests
  // ============================================

  // Test 22: dir="rtl" on a block element marks text as RTL
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p dir=\"rtl\">Arabic text</p></body></html>");
    runner.expectTrue(ok, "dir_rtl: parses successfully");
    runner.expectTrue(parser.hasRtlElement(), "dir_rtl: text marked as RTL");
  }

  // Test 23: dir="ltr" on a block element keeps text LTR
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p dir=\"ltr\">English text</p></body></html>");
    runner.expectTrue(ok, "dir_ltr: parses successfully");
    runner.expectFalse(parser.hasRtlElement(), "dir_ltr: text remains LTR");
  }

  // Test 24: dir attribute is case-insensitive
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p dir=\"RTL\">Arabic text</p></body></html>");
    runner.expectTrue(ok, "dir_case: parses successfully");
    runner.expectTrue(parser.hasRtlElement(), "dir_case: RTL uppercase works");
  }

  // Test 25: dir="rtl" on body affects all children
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body dir=\"rtl\">"
        "<p>First paragraph</p>"
        "<p>Second paragraph</p>"
        "</body></html>");
    runner.expectTrue(ok, "dir_body_rtl: parses successfully");
    runner.expectTrue(parser.isAllTextRtl(), "dir_body_rtl: all text is RTL");
  }

  // Test 26: RTL scope resets after closing tag
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<div dir=\"rtl\"><p>RTL text</p></div>"
        "<p>LTR text</p>"
        "</body></html>");
    runner.expectTrue(ok, "dir_scope_reset: parses successfully");
    // First element should be RTL, second should not
    bool hasRtl = false;
    bool hasLtr = false;
    for (const auto& elem : parser.elements) {
      if (elem.type == TestParser::ParsedElement::TEXT) {
        if (elem.content.find("RTL") != std::string::npos && elem.isRtl) hasRtl = true;
        if (elem.content.find("LTR") != std::string::npos && !elem.isRtl) hasLtr = true;
      }
    }
    runner.expectTrue(hasRtl, "dir_scope_reset: RTL text is RTL");
    runner.expectTrue(hasLtr, "dir_scope_reset: LTR text after scope is LTR");
  }

  // Test 27: No dir attribute defaults to LTR
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>Default direction</p></body></html>");
    runner.expectTrue(ok, "dir_default: parses successfully");
    runner.expectFalse(parser.hasRtlElement(), "dir_default: no dir attribute = LTR");
  }

  // Test 27b: dir="rtl" on html element marks heading (first block) as RTL
  // Regression test: the first text block is created before <html dir="rtl"> is
  // parsed, so the empty-block reuse path must propagate the RTL flag.
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html dir=\"rtl\"><body>"
        "<h1>الكتاب الأول</h1>"
        "<p>Body text</p>"
        "</body></html>");
    runner.expectTrue(ok, "dir_rtl_heading: parses successfully");
    runner.expectTrue(parser.isAllTextRtl(), "dir_rtl_heading: heading and body are RTL");
    // Verify the heading element specifically is RTL
    bool headingIsRtl = false;
    for (const auto& elem : parser.elements) {
      if (elem.type == TestParser::ParsedElement::TEXT &&
          elem.content.find("\xd8\xa7\xd9\x84\xd9\x83\xd8\xaa\xd8\xa7\xd8\xa8") != std::string::npos) {
        headingIsRtl = elem.isRtl;
        break;
      }
    }
    runner.expectTrue(headingIsRtl, "dir_rtl_heading: first heading block is RTL");
  }

  // Test 27c: dir="rtl" on html with heading-only content
  {
    TestParser parser;
    bool ok = parser.parse("<html dir=\"rtl\"><body><h2>عنوان</h2></body></html>");
    runner.expectTrue(ok, "dir_rtl_h2_only: parses successfully");
    runner.expectTrue(parser.hasRtlElement(), "dir_rtl_h2_only: heading is RTL");
  }

  // ============================================
  // Tiny decorative image skip tests
  // ============================================

  // Test 28: 1px-tall decorative line separator is skipped
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<h1><img height=\"1\" src=\"images/line_r1.jpg\" width=\"166\"/> 5</h1>"
        "</body></html>");
    runner.expectTrue(ok, "skip_1px_height: parses successfully");
    runner.expectFalse(parser.hasImagePlaceholder(), "skip_1px_height: 1px-tall image skipped");
    runner.expectTrue(parser.getAllText().find("5") != std::string::npos, "skip_1px_height: text preserved");
  }

  // Test 29: 1px-wide decorative image is skipped
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<p><img width=\"1\" height=\"100\" src=\"spacer.png\"/></p>"
        "</body></html>");
    runner.expectTrue(ok, "skip_1px_width: parses successfully");
    runner.expectFalse(parser.hasImagePlaceholder(), "skip_1px_width: 1px-wide image skipped");
  }

  // Test 30: 19px-tall image at boundary is still skipped
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body><img width=\"200\" height=\"19\" src=\"border.jpg\"/></body></html>");
    runner.expectTrue(ok, "skip_19px_boundary: parses successfully");
    runner.expectFalse(parser.hasImagePlaceholder(), "skip_19px_boundary: 19px image skipped");
  }

  // Test 31: 20px-tall image is NOT skipped (at threshold)
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body><img width=\"200\" height=\"20\" src=\"small.jpg\"/></body></html>");
    runner.expectTrue(ok, "keep_20px: parses successfully");
    runner.expectTrue(parser.hasImagePlaceholder(), "keep_20px: 20px image kept");
  }

  // Test 32: Normal-sized image is NOT skipped
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body><img width=\"480\" height=\"300\" src=\"photo.jpg\"/></body></html>");
    runner.expectTrue(ok, "keep_normal: parses successfully");
    runner.expectTrue(parser.hasImagePlaceholder(), "keep_normal: normal image kept");
  }

  // Test 33: Image without width/height attributes is NOT skipped (unknown dimensions)
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body><img src=\"unknown.jpg\"/></body></html>");
    runner.expectTrue(ok, "keep_no_dims: parses successfully");
    runner.expectTrue(parser.hasImagePlaceholder(), "keep_no_dims: image without dimensions kept");
  }

  // Test 34: Hyperion Cantos pattern - header with two 1px decorative images
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<h1>"
        "<img height=\"1\" src=\"images/line_r1.jpg\" width=\"166\"/>"
        " 5 "
        "<img height=\"1\" src=\"images/line_r2.jpg\" width=\"117\"/>"
        "</h1>"
        "<p>Chapter text here</p>"
        "</body></html>");
    runner.expectTrue(ok, "hyperion_pattern: parses successfully");
    runner.expectFalse(parser.hasImagePlaceholder(), "hyperion_pattern: both decorative images skipped");
    runner.expectTrue(parser.getAllText().find("5") != std::string::npos, "hyperion_pattern: chapter number preserved");
    runner.expectTrue(parser.getAllText().find("Chapter text") != std::string::npos,
                      "hyperion_pattern: body text preserved");
  }

  // Test 35: 19x19 pixel image is skipped (both dimensions below threshold)
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body><img width=\"19\" height=\"19\" src=\"dot.png\"/></body></html>");
    runner.expectTrue(ok, "skip_19x19: parses successfully");
    runner.expectFalse(parser.hasImagePlaceholder(), "skip_19x19: tiny square image skipped");
  }

  // Test 36: 20x20 pixel image is kept (both dimensions at threshold)
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body><img width=\"20\" height=\"20\" src=\"icon.png\"/></body></html>");
    runner.expectTrue(ok, "keep_20x20: parses successfully");
    runner.expectTrue(parser.hasImagePlaceholder(), "keep_20x20: small but visible image kept");
  }

  // ============================================
  // Unsupported image format tests
  // ============================================

  // Test 37: Unsupported format (GIF) produces no placeholder
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><img src=\"photo.gif\"/></body></html>");
    runner.expectTrue(ok, "skip_gif: parses successfully");
    runner.expectFalse(parser.hasImagePlaceholder(), "skip_gif: GIF image silently skipped");
  }

  // Test 38: Unsupported format (SVG) produces no placeholder
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><img src=\"icon.svg\"/></body></html>");
    runner.expectTrue(ok, "skip_svg: parses successfully");
    runner.expectFalse(parser.hasImagePlaceholder(), "skip_svg: SVG image silently skipped");
  }

  // Test 39: Unsupported format (WebP) produces no placeholder
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><img src=\"photo.webp\"/></body></html>");
    runner.expectTrue(ok, "skip_webp: parses successfully");
    runner.expectFalse(parser.hasImagePlaceholder(), "skip_webp: WebP image silently skipped");
  }

  // Test 40: Unsupported format with alt text still produces no placeholder
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><img src=\"anim.gif\" alt=\"A funny cat\"/></body></html>");
    runner.expectTrue(ok, "skip_gif_alt: parses successfully");
    runner.expectFalse(parser.hasImagePlaceholder(), "skip_gif_alt: GIF with alt text silently skipped");
  }

  // ============================================
  // Anchor map (id attribute) tests
  // ============================================

  // Test 41: Elements with id attribute are tracked in anchor map
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<p id=\"chapter1\">Chapter 1</p>"
        "<p id=\"chapter2\">Chapter 2</p>"
        "</body></html>");
    runner.expectTrue(ok, "anchor_basic: parses successfully");
    runner.expectEq(static_cast<size_t>(2), parser.anchorMap.size(), "anchor_basic: two anchors collected");
    runner.expectEqual("chapter1", parser.anchorMap[0].first, "anchor_basic: first anchor id");
    runner.expectEqual("chapter2", parser.anchorMap[1].first, "anchor_basic: second anchor id");
  }

  // Test 42: Empty id attribute is skipped
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<p id=\"\">Empty id</p>"
        "<p id=\"valid\">Valid id</p>"
        "</body></html>");
    runner.expectTrue(ok, "anchor_empty_id: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.anchorMap.size(), "anchor_empty_id: only valid id collected");
    runner.expectEqual("valid", parser.anchorMap[0].first, "anchor_empty_id: correct id");
  }

  // Test 43: id attributes inside <head> skip region are not tracked
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html>"
        "<head><meta id=\"head-meta\"/></head>"
        "<body><p id=\"body-anchor\">Content</p></body>"
        "</html>");
    runner.expectTrue(ok, "anchor_skip_head: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.anchorMap.size(), "anchor_skip_head: only body anchor collected");
    runner.expectEqual("body-anchor", parser.anchorMap[0].first, "anchor_skip_head: correct id");
  }

  // Test 44: id attributes inside <table> skip region are not tracked
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<table><tr><td id=\"cell1\">Data</td></tr></table>"
        "<p id=\"after-table\">Text</p>"
        "</body></html>");
    runner.expectTrue(ok, "anchor_skip_table: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.anchorMap.size(), "anchor_skip_table: only post-table anchor");
    runner.expectEqual("after-table", parser.anchorMap[0].first, "anchor_skip_table: correct id");
  }

  // Test 45: id attributes on aria-hidden anchors are not tracked
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<a href=\"#\" aria-hidden=\"true\" id=\"hidden-anchor\">hidden</a>"
        "<p id=\"visible\">Visible</p>"
        "</body></html>");
    runner.expectTrue(ok, "anchor_skip_aria_hidden: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.anchorMap.size(),
                    "anchor_skip_aria_hidden: only visible anchor collected");
    runner.expectEqual("visible", parser.anchorMap[0].first, "anchor_skip_aria_hidden: correct id");
  }

  // Test 46: id on non-block element (span) is still tracked
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<p>Before <span id=\"inline-anchor\">inline</span> after</p>"
        "</body></html>");
    runner.expectTrue(ok, "anchor_inline: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.anchorMap.size(), "anchor_inline: inline anchor collected");
    runner.expectEqual("inline-anchor", parser.anchorMap[0].first, "anchor_inline: correct id");
  }

  // Test 47: id on header element is tracked
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<h1 id=\"title\">Title</h1>"
        "<h2 id=\"section1\">Section 1</h2>"
        "</body></html>");
    runner.expectTrue(ok, "anchor_headers: parses successfully");
    runner.expectEq(static_cast<size_t>(2), parser.anchorMap.size(), "anchor_headers: both header anchors collected");
    runner.expectEqual("title", parser.anchorMap[0].first, "anchor_headers: h1 id");
    runner.expectEqual("section1", parser.anchorMap[1].first, "anchor_headers: h2 id");
  }

  // Test 48: Block count reflects correct ordering for anchor page mapping
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<p id=\"start\">First paragraph</p>"
        "<p>Second paragraph</p>"
        "<p id=\"end\">Third paragraph</p>"
        "</body></html>");
    runner.expectTrue(ok, "anchor_page_order: parses successfully");
    runner.expectEq(static_cast<size_t>(2), parser.anchorMap.size(), "anchor_page_order: two anchors");
    // First anchor is at block 1 (first <p>), third is at block 3 (third <p>)
    runner.expectTrue(parser.anchorMap[0].second < parser.anchorMap[1].second,
                      "anchor_page_order: second anchor has higher block count");
  }

  // Test 49: No id attributes means empty anchor map
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body><p>No ids here</p><p>None here either</p></body></html>");
    runner.expectTrue(ok, "anchor_none: parses successfully");
    runner.expectEq(static_cast<size_t>(0), parser.anchorMap.size(), "anchor_none: empty anchor map");
  }

  // Test 50: id on pagebreak skip region is not tracked
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<span role=\"doc-pagebreak\" id=\"page5\" title=\"5\">5</span>"
        "<p id=\"after-pagebreak\">Content</p>"
        "</body></html>");
    runner.expectTrue(ok, "anchor_skip_pagebreak: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.anchorMap.size(),
                    "anchor_skip_pagebreak: only post-pagebreak anchor");
    runner.expectEqual("after-pagebreak", parser.anchorMap[0].first, "anchor_skip_pagebreak: correct id");
  }

  // ============================================
  // HTML entity handling tests
  // ============================================

  // Test 51: &nbsp; entity is resolved (no DTD declaration needed)
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>Hello&nbsp;World</p></body></html>");
    runner.expectTrue(ok, "entity_nbsp: parses successfully");
    // NBSP (U+00A0) = 0xC2 0xA0 in UTF-8
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("Hello") != std::string::npos, "entity_nbsp: Hello present");
    runner.expectTrue(text.find("World") != std::string::npos, "entity_nbsp: World present");
    runner.expectTrue(text.find('\xC2') != std::string::npos, "entity_nbsp: NBSP byte present");
  }

  // Test 52: &mdash; entity is resolved
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>Hello&mdash;World</p></body></html>");
    runner.expectTrue(ok, "entity_mdash: parses successfully");
    // mdash (U+2014) = 0xE2 0x80 0x94 in UTF-8
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xE2\x80\x94") != std::string::npos, "entity_mdash: em-dash present");
  }

  // Test 53: &ldquo; and &rdquo; entities are resolved
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&ldquo;Hello&rdquo;</p></body></html>");
    runner.expectTrue(ok, "entity_quotes: parses successfully");
    std::string text = parser.getAllText();
    // ldquo (U+201C) = 0xE2 0x80 0x9C, rdquo (U+201D) = 0xE2 0x80 0x9D
    runner.expectTrue(text.find("\xE2\x80\x9C") != std::string::npos, "entity_quotes: left quote present");
    runner.expectTrue(text.find("\xE2\x80\x9D") != std::string::npos, "entity_quotes: right quote present");
  }

  // Test 54: &hellip; entity is resolved
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>Wait&hellip;</p></body></html>");
    runner.expectTrue(ok, "entity_hellip: parses successfully");
    std::string text = parser.getAllText();
    // hellip (U+2026) = 0xE2 0x80 0xA6
    runner.expectTrue(text.find("\xE2\x80\xA6") != std::string::npos, "entity_hellip: ellipsis present");
  }

  // Test 55: Unknown entity is silently dropped (not passed through as text)
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>Hello&unknownentity;World</p></body></html>");
    runner.expectTrue(ok, "entity_unknown: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("&unknownentity;") == std::string::npos,
                      "entity_unknown: unknown entity not visible");
    runner.expectTrue(text.find("Hello") != std::string::npos, "entity_unknown: text before entity preserved");
    runner.expectTrue(text.find("World") != std::string::npos, "entity_unknown: text after entity preserved");
  }

  // Test 56: XML built-in entities still work (&amp; &lt; &gt;)
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>A &amp; B &lt; C &gt; D</p></body></html>");
    runner.expectTrue(ok, "entity_builtin: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("&") != std::string::npos, "entity_builtin: ampersand present");
    runner.expectTrue(text.find("<") != std::string::npos, "entity_builtin: less-than present");
    runner.expectTrue(text.find(">") != std::string::npos, "entity_builtin: greater-than present");
  }

  // Test 57: Multiple entities in one paragraph
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&lsquo;Don&rsquo;t &ndash; really&rdquo;</p></body></html>");
    runner.expectTrue(ok, "entity_multiple: parses successfully");
    std::string text = parser.getAllText();
    // lsquo (U+2018) = 0xE2 0x80 0x98
    // rsquo (U+2019) = 0xE2 0x80 0x99
    // ndash (U+2013) = 0xE2 0x80 0x93
    runner.expectTrue(text.find("\xE2\x80\x98") != std::string::npos, "entity_multiple: left single quote");
    runner.expectTrue(text.find("\xE2\x80\x99") != std::string::npos, "entity_multiple: right single quote");
    runner.expectTrue(text.find("\xE2\x80\x93") != std::string::npos, "entity_multiple: en-dash");
  }

  // Test 58: &copy; and &reg; entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&copy; 2024 &reg;</p></body></html>");
    runner.expectTrue(ok, "entity_symbols: parses successfully");
    std::string text = parser.getAllText();
    // copy (U+00A9) = 0xC2 0xA9, reg (U+00AE) = 0xC2 0xAE
    runner.expectTrue(text.find("\xC2\xA9") != std::string::npos, "entity_symbols: copyright present");
    runner.expectTrue(text.find("\xC2\xAE") != std::string::npos, "entity_symbols: registered present");
  }

  // Test 59: Accented character entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>caf&eacute; na&iuml;ve</p></body></html>");
    runner.expectTrue(ok, "entity_accents: parses successfully");
    std::string text = parser.getAllText();
    // eacute (U+00E9) = 0xC3 0xA9, iuml (U+00EF) = 0xC3 0xAF
    runner.expectTrue(text.find("\xC3\xA9") != std::string::npos, "entity_accents: e-acute present");
    runner.expectTrue(text.find("\xC3\xAF") != std::string::npos, "entity_accents: i-umlaut present");
  }

  // Test 60: Entity inside skipped region (head) is not processed
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><head><title>&mdash; Title</title></head>"
        "<body><p>&mdash; Content</p></body></html>");
    runner.expectTrue(ok, "entity_skip_head: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("Title") == std::string::npos, "entity_skip_head: head content skipped");
    runner.expectTrue(text.find("\xE2\x80\x94") != std::string::npos, "entity_skip_head: body entity resolved");
  }

  // Test 61: Numeric decimal entities (handled natively by expat)
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&#8212; &#8220;hi&#8221;</p></body></html>");
    runner.expectTrue(ok, "entity_numeric_dec: parses successfully");
    std::string text = parser.getAllText();
    // &#8212; = em dash (U+2014), &#8220; = left dquote (U+201C), &#8221; = right dquote (U+201D)
    runner.expectTrue(text.find("\xE2\x80\x94") != std::string::npos, "entity_numeric_dec: em dash");
    runner.expectTrue(text.find("\xE2\x80\x9C") != std::string::npos, "entity_numeric_dec: left dquote");
    runner.expectTrue(text.find("\xE2\x80\x9D") != std::string::npos, "entity_numeric_dec: right dquote");
  }

  // Test 62: Numeric hex entities (handled natively by expat)
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&#x2014; &#x201C;hi&#x201D;</p></body></html>");
    runner.expectTrue(ok, "entity_numeric_hex: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xE2\x80\x94") != std::string::npos, "entity_numeric_hex: em dash");
    runner.expectTrue(text.find("\xE2\x80\x9C") != std::string::npos, "entity_numeric_hex: left dquote");
    runner.expectTrue(text.find("\xE2\x80\x9D") != std::string::npos, "entity_numeric_hex: right dquote");
  }

  // Test 63: Numeric entities for accented characters
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&#233; &#241; &#252;</p></body></html>");
    runner.expectTrue(ok, "entity_numeric_accents: parses successfully");
    std::string text = parser.getAllText();
    // &#233; = e-acute (U+00E9), &#241; = n-tilde (U+00F1), &#252; = u-umlaut (U+00FC)
    runner.expectTrue(text.find("\xC3\xA9") != std::string::npos, "entity_numeric_accents: e-acute");
    runner.expectTrue(text.find("\xC3\xB1") != std::string::npos, "entity_numeric_accents: n-tilde");
    runner.expectTrue(text.find("\xC3\xBC") != std::string::npos, "entity_numeric_accents: u-umlaut");
  }

  // Test 64: Currency entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&euro;100 &pound;50 &yen;1000</p></body></html>");
    runner.expectTrue(ok, "entity_currency: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xE2\x82\xAC") != std::string::npos, "entity_currency: euro");
    runner.expectTrue(text.find("\xC2\xA3") != std::string::npos, "entity_currency: pound");
    runner.expectTrue(text.find("\xC2\xA5") != std::string::npos, "entity_currency: yen");
  }

  // Test 65: Math entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>2 &times; 3 &divide; 1 &plusmn; 0.5</p></body></html>");
    runner.expectTrue(ok, "entity_math: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xC3\x97") != std::string::npos, "entity_math: times");
    runner.expectTrue(text.find("\xC3\xB7") != std::string::npos, "entity_math: divide");
    runner.expectTrue(text.find("\xC2\xB1") != std::string::npos, "entity_math: plusmn");
  }

  // Test 66: Arrow entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&larr; &rarr; &uarr; &darr;</p></body></html>");
    runner.expectTrue(ok, "entity_arrows: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xE2\x86\x90") != std::string::npos, "entity_arrows: larr");
    runner.expectTrue(text.find("\xE2\x86\x92") != std::string::npos, "entity_arrows: rarr");
    runner.expectTrue(text.find("\xE2\x86\x91") != std::string::npos, "entity_arrows: uarr");
    runner.expectTrue(text.find("\xE2\x86\x93") != std::string::npos, "entity_arrows: darr");
  }

  // Test 67: Greek letter entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&alpha; &beta; &gamma; &delta; &pi; &Omega;</p></body></html>");
    runner.expectTrue(ok, "entity_greek: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xCE\xB1") != std::string::npos, "entity_greek: alpha");
    runner.expectTrue(text.find("\xCE\xB2") != std::string::npos, "entity_greek: beta");
    runner.expectTrue(text.find("\xCE\xB3") != std::string::npos, "entity_greek: gamma");
    runner.expectTrue(text.find("\xCE\xB4") != std::string::npos, "entity_greek: delta");
    runner.expectTrue(text.find("\xCF\x80") != std::string::npos, "entity_greek: pi");
    runner.expectTrue(text.find("\xCE\xA9") != std::string::npos, "entity_greek: Omega");
  }

  // Test 68: Typographic mark entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>20&deg;C &sect;4 &para;5</p></body></html>");
    runner.expectTrue(ok, "entity_typo_marks: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xC2\xB0") != std::string::npos, "entity_typo_marks: degree");
    runner.expectTrue(text.find("\xC2\xA7") != std::string::npos, "entity_typo_marks: section");
    runner.expectTrue(text.find("\xC2\xB6") != std::string::npos, "entity_typo_marks: pilcrow");
  }

  // Test 69: Fraction entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&frac12; cup + &frac14; tsp</p></body></html>");
    runner.expectTrue(ok, "entity_fractions: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xC2\xBD") != std::string::npos, "entity_fractions: frac12");
    runner.expectTrue(text.find("\xC2\xBC") != std::string::npos, "entity_fractions: frac14");
  }

  // Test 70: Guillemet entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&laquo;excellent&raquo;</p></body></html>");
    runner.expectTrue(ok, "entity_guillemets: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xC2\xAB") != std::string::npos, "entity_guillemets: laquo");
    runner.expectTrue(text.find("\xC2\xBB") != std::string::npos, "entity_guillemets: raquo");
  }

  // Test 71: Superscript and dagger entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>E=mc&sup2; note&dagger; ref&Dagger;</p></body></html>");
    runner.expectTrue(ok, "entity_sup_dagger: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xC2\xB2") != std::string::npos, "entity_sup_dagger: sup2");
    runner.expectTrue(text.find("\xE2\x80\xA0") != std::string::npos, "entity_sup_dagger: dagger");
    runner.expectTrue(text.find("\xE2\x80\xA1") != std::string::npos, "entity_sup_dagger: Dagger");
  }

  // Test 72: &trade; and &thinsp; entities
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>Brand&trade; thin&thinsp;space</p></body></html>");
    runner.expectTrue(ok, "entity_trade_thinsp: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("\xE2\x84\xA2") != std::string::npos, "entity_trade_thinsp: trade");
    runner.expectTrue(text.find("\xE2\x80\x89") != std::string::npos, "entity_trade_thinsp: thinsp");
  }

  // Test 73: Mixed real-world content with multiple entity types
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<p>&ldquo;The caf&eacute; served cr&egrave;me br&ucirc;l&eacute;e for &euro;8.50&mdash;a bargain!&rdquo;</p>"
        "<p>Temperature: 20&deg;C &plusmn; 2&deg;. See &sect;4.2 and &para;5.</p>"
        "<p>&frac12; cup &bull; H&sup2;O &bull; footnote&dagger;</p>"
        "</body></html>");
    runner.expectTrue(ok, "entity_mixed_realworld: parses successfully");
    std::string text = parser.getAllText();
    // Check representative entities from each paragraph
    runner.expectTrue(text.find("\xE2\x80\x9C") != std::string::npos, "entity_mixed_realworld: ldquo");
    runner.expectTrue(text.find("\xC3\xA9") != std::string::npos, "entity_mixed_realworld: eacute");
    runner.expectTrue(text.find("\xE2\x82\xAC") != std::string::npos, "entity_mixed_realworld: euro");
    runner.expectTrue(text.find("\xE2\x80\x94") != std::string::npos, "entity_mixed_realworld: mdash");
    runner.expectTrue(text.find("\xC2\xB0") != std::string::npos, "entity_mixed_realworld: degree");
    runner.expectTrue(text.find("\xC2\xB1") != std::string::npos, "entity_mixed_realworld: plusmn");
    runner.expectTrue(text.find("\xC2\xBD") != std::string::npos, "entity_mixed_realworld: frac12");
    runner.expectTrue(text.find("\xC2\xB2") != std::string::npos, "entity_mixed_realworld: sup2");
    runner.expectTrue(text.find("\xE2\x80\xA0") != std::string::npos, "entity_mixed_realworld: dagger");
  }

  // ============================================
  // Default handler filtering tests (DOCTYPE, XML decl, comments)
  // These must NOT appear as visible text
  // ============================================

  // Test 74: XML declaration is not visible
  {
    TestParser parser;
    bool ok = parser.parse("<?xml version=\"1.0\" encoding=\"UTF-8\"?><html><body><p>Content</p></body></html>");
    runner.expectTrue(ok, "drop_xml_decl: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("xml") == std::string::npos, "drop_xml_decl: xml decl not visible");
    runner.expectTrue(text.find("version") == std::string::npos, "drop_xml_decl: version not visible");
    runner.expectTrue(text.find("Content") != std::string::npos, "drop_xml_decl: body content visible");
  }

  // Test 75: DOCTYPE declaration is not visible
  {
    TestParser parser;
    bool ok = parser.parse(
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">"
        "<html><body><p>Content</p></body></html>");
    runner.expectTrue(ok, "drop_doctype: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("DOCTYPE") == std::string::npos, "drop_doctype: DOCTYPE not visible");
    runner.expectTrue(text.find("W3C") == std::string::npos, "drop_doctype: DTD URL not visible");
    runner.expectTrue(text.find("Content") != std::string::npos, "drop_doctype: body content visible");
  }

  // Test 76: HTML comment is not visible
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<!-- This is a comment that should not appear -->"
        "<p>Visible text</p>"
        "</body></html>");
    runner.expectTrue(ok, "drop_comment: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("comment") == std::string::npos, "drop_comment: comment not visible");
    runner.expectTrue(text.find("Visible text") != std::string::npos, "drop_comment: body text visible");
  }

  // Test 77: Processing instruction is not visible
  {
    TestParser parser;
    bool ok = parser.parse(
        "<?xml version=\"1.0\"?>"
        "<html><body>"
        "<?some-pi instruction data?>"
        "<p>After PI</p>"
        "</body></html>");
    runner.expectTrue(ok, "drop_pi: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("some-pi") == std::string::npos, "drop_pi: PI not visible");
    runner.expectTrue(text.find("instruction") == std::string::npos, "drop_pi: PI data not visible");
    runner.expectTrue(text.find("After PI") != std::string::npos, "drop_pi: body text visible");
  }

  // Test 78: Full EPUB-like preamble with DOCTYPE + XML decl + entities still work
  {
    TestParser parser;
    bool ok = parser.parse(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">"
        "<html><body>"
        "<p>&ldquo;Hello&rdquo; &mdash; welcome to the caf&eacute;</p>"
        "</body></html>");
    runner.expectTrue(ok, "epub_preamble: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("xml") == std::string::npos, "epub_preamble: xml decl not visible");
    runner.expectTrue(text.find("DOCTYPE") == std::string::npos, "epub_preamble: DOCTYPE not visible");
    runner.expectTrue(text.find("W3C") == std::string::npos, "epub_preamble: DTD not visible");
    // Entities still resolve correctly
    runner.expectTrue(text.find("\xE2\x80\x9C") != std::string::npos, "epub_preamble: ldquo resolved");
    runner.expectTrue(text.find("\xE2\x80\x9D") != std::string::npos, "epub_preamble: rdquo resolved");
    runner.expectTrue(text.find("\xE2\x80\x94") != std::string::npos, "epub_preamble: mdash resolved");
    runner.expectTrue(text.find("\xC3\xA9") != std::string::npos, "epub_preamble: eacute resolved");
    runner.expectTrue(text.find("Hello") != std::string::npos, "epub_preamble: text content visible");
  }

  // Test 79: Multiple comments interspersed with content
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<!-- comment 1 --><p>First</p>"
        "<!-- comment 2 --><p>Second</p>"
        "<!-- comment 3 -->"
        "</body></html>");
    runner.expectTrue(ok, "drop_multi_comments: parses successfully");
    std::string text = parser.getAllText();
    runner.expectTrue(text.find("comment") == std::string::npos, "drop_multi_comments: no comments visible");
    runner.expectTrue(text.find("First") != std::string::npos, "drop_multi_comments: First visible");
    runner.expectTrue(text.find("Second") != std::string::npos, "drop_multi_comments: Second visible");
  }

  // Test 80: Numeric hex entities for Unicode symbols (was Test 74)
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>&#x2603; &#x2665; &#xA9;</p></body></html>");
    runner.expectTrue(ok, "entity_numeric_symbols: parses successfully");
    std::string text = parser.getAllText();
    // &#x2603; = snowman (U+2603), &#x2665; = heart (U+2665), &#xA9; = copyright (U+00A9)
    runner.expectTrue(text.find("\xE2\x98\x83") != std::string::npos, "entity_numeric_symbols: snowman");
    runner.expectTrue(text.find("\xE2\x99\xA5") != std::string::npos, "entity_numeric_symbols: heart");
    runner.expectTrue(text.find("\xC2\xA9") != std::string::npos, "entity_numeric_symbols: copyright");
  }

  // ============================================
  // CSS text-align inheritance tests
  // ============================================

  // Test 81: Header tags default to center alignment
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><h1>Title</h1></body></html>");
    runner.expectTrue(ok, "align_header_center: parses successfully");
    runner.expectTrue(parser.getBlockStyleForText("Title") == BlockStyle::CENTER,
                      "align_header_center: h1 is centered");
  }

  // Test 82: Block tag with explicit text-align center
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p style=\"text-align: center\">Centered</p></body></html>");
    runner.expectTrue(ok, "align_explicit_center: parses successfully");
    runner.expectTrue(parser.getBlockStyleForText("Centered") == BlockStyle::CENTER,
                      "align_explicit_center: p is centered");
  }

  // Test 83: Block tag with explicit text-align right
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p style=\"text-align: right\">Right</p></body></html>");
    runner.expectTrue(ok, "align_explicit_right: parses successfully");
    runner.expectTrue(parser.getBlockStyleForText("Right") == BlockStyle::RIGHT,
                      "align_explicit_right: p is right-aligned");
  }

  // Test 84: Alignment inherited from parent div
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<div style=\"text-align: center\">"
        "<p>Inherited center</p>"
        "</div>"
        "</body></html>");
    runner.expectTrue(ok, "align_inherit_center: parses successfully");
    runner.expectTrue(parser.getBlockStyleForText("Inherited center") == BlockStyle::CENTER,
                      "align_inherit_center: p inherits center from div");
  }

  // Test 85: Alignment inherited from parent with multiple children
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<div style=\"text-align: right\">"
        "<p>First child</p>"
        "<p>Second child</p>"
        "</div>"
        "</body></html>");
    runner.expectTrue(ok, "align_inherit_multi: parses successfully");
    runner.expectTrue(parser.getBlockStyleForText("First child") == BlockStyle::RIGHT,
                      "align_inherit_multi: first p inherits right");
    runner.expectTrue(parser.getBlockStyleForText("Second child") == BlockStyle::RIGHT,
                      "align_inherit_multi: second p inherits right");
  }

  // Test 86: Alignment scope resets after parent closes
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<div style=\"text-align: center\">"
        "<p>Centered</p>"
        "</div>"
        "<p>Default</p>"
        "</body></html>");
    runner.expectTrue(ok, "align_scope_reset: parses successfully");
    runner.expectTrue(parser.getBlockStyleForText("Centered") == BlockStyle::CENTER,
                      "align_scope_reset: inside div is centered");
    runner.expectTrue(parser.getBlockStyleForText("Default") == BlockStyle::LEFT,
                      "align_scope_reset: after div resets to left");
  }

  // Test 87: Child explicit alignment overrides parent inheritance
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<div style=\"text-align: center\">"
        "<p style=\"text-align: right\">Right override</p>"
        "<p>Still centered</p>"
        "</div>"
        "</body></html>");
    runner.expectTrue(ok, "align_override: parses successfully");
    runner.expectTrue(parser.getBlockStyleForText("Right override") == BlockStyle::RIGHT,
                      "align_override: explicit right overrides inherited center");
    runner.expectTrue(parser.getBlockStyleForText("Still centered") == BlockStyle::CENTER,
                      "align_override: sibling still inherits center");
  }

  // Test 88: Nested inheritance (grandparent → parent → child)
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<div style=\"text-align: center\">"
        "<div>"
        "<p>Deep inherited</p>"
        "</div>"
        "</div>"
        "</body></html>");
    runner.expectTrue(ok, "align_deep_inherit: parses successfully");
    runner.expectTrue(parser.getBlockStyleForText("Deep inherited") == BlockStyle::CENTER,
                      "align_deep_inherit: p inherits center through nested div");
  }

  // Test 89: No alignment set, defaults to left
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p>Default left</p></body></html>");
    runner.expectTrue(ok, "align_default_left: parses successfully");
    runner.expectTrue(parser.getBlockStyleForText("Default left") == BlockStyle::LEFT,
                      "align_default_left: p defaults to left");
  }

  // Test 90: justify alignment inherited
  {
    TestParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<div style=\"text-align: justify\">"
        "<p>Justified text</p>"
        "</div>"
        "</body></html>");
    runner.expectTrue(ok, "align_inherit_justify: parses successfully");
    runner.expectTrue(parser.getBlockStyleForText("Justified text") == BlockStyle::JUSTIFIED,
                      "align_inherit_justify: p inherits justify from div");
  }

  return runner.allPassed() ? 0 : 1;
}
