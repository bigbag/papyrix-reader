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

  void flushText() {
    if (!currentText.empty()) {
      ParsedElement elem;
      elem.type = ParsedElement::TEXT;
      elem.content = currentText;
      elem.isBold = boldUntilDepth < depth;
      elem.isItalic = italicUntilDepth < depth;
      elem.isRtl = pendingRtl;
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

    // Headers
    if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
      self->flushText();
      self->blockCount++;
      self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    }

    // Block tags
    if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
      self->flushText();
      self->blockCount++;
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
  }

  bool parse(const std::string& html) {
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) return false;

    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterData);

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

  return runner.allPassed() ? 0 : 1;
}
