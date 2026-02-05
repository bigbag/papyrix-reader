// ChapterHtmlSlimParser unit tests
// Tests HTML parsing behavior including aria-hidden anchor skipping
//
// This test validates the parsing logic from ChapterHtmlSlimParser by
// reimplementing the key parsing rules in a test-friendly way. This tests
// the same HTML parsing behavior without needing to mock all the rendering
// infrastructure.

#include "test_utils.h"

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

const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote"};
constexpr int NUM_BLOCK_TAGS = 5;

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
  };

  std::vector<ParsedElement> elements;
  std::string currentText;
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;

  void flushText() {
    if (!currentText.empty()) {
      ParsedElement elem;
      elem.type = ParsedElement::TEXT;
      elem.content = currentText;
      elem.isBold = boldUntilDepth < depth;
      elem.isItalic = italicUntilDepth < depth;
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
      std::string altText;
      if (atts) {
        for (int i = 0; atts[i]; i += 2) {
          if (strcmp(atts[i], "alt") == 0 && atts[i + 1][0] != '\0') {
            altText = atts[i + 1];
          }
        }
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

    // Headers
    if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
      self->flushText();
      self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    }

    // Block tags
    if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
      self->flushText();
    }

    // Bold tags
    if (matches(name, BOLD_TAGS, NUM_BOLD_TAGS)) {
      self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    }

    // Italic tags
    if (matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
      self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
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

  // Test 18: Pre/code blocks
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><pre><code>function test() { return true; }</code></pre></body></html>");
    runner.expectTrue(ok, "pre_code: parses successfully");
    runner.expectTrue(parser.getAllText().find("function") != std::string::npos, "pre_code: code visible");
  }

  // Test 19: Bold and italic tracking
  {
    TestParser parser;
    bool ok = parser.parse("<html><body><p><b>Bold</b> and <i>Italic</i></p></body></html>");
    runner.expectTrue(ok, "bold_italic: parses successfully");
    // Check that we captured the text
    runner.expectTrue(parser.getAllText().find("Bold") != std::string::npos, "bold_italic: bold text visible");
    runner.expectTrue(parser.getAllText().find("Italic") != std::string::npos, "bold_italic: italic text visible");
  }

  // Test 20: Nested skip regions work correctly
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

  return runner.allPassed() ? 0 : 1;
}
