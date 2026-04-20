// Fb2 inline style tracking unit tests
//
// Mirrors the bold/italic depth-tracking subset of Fb2Parser.cpp without
// depending on TextBlock, GfxRenderer, or the font manager. Covers:
//   - <emphasis> -> italic
//   - <code>     -> italic  (mirrors <emphasis>, added for issue #94)
//   - <strong>   -> bold
//   - nested style interactions (bold+italic combinations)
//   - scope reset on end-tag

#include "test_utils.h"

#include <EncodingDetector.h>
#include <ExpatEncodingHandler.h>
#include <expat.h>

#include <climits>
#include <cstring>
#include <string>
#include <vector>

struct StyledRun {
  std::string text;
  bool bold;
  bool italic;
};

// Lightweight FB2 style-tracking harness. Only implements what's needed to
// observe bold/italic flags flowing through Fb2Parser's startElement/endElement.
class TestFb2StyleParser {
  int depth_ = 0;
  int boldUntilDepth_ = INT_MAX;
  int italicUntilDepth_ = INT_MAX;

  static const char* stripNamespace(const XML_Char* name) {
    const char* tag = strrchr(name, ':');
    return tag ? tag + 1 : name;
  }

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** /*atts*/) {
    auto* self = static_cast<TestFb2StyleParser*>(userData);
    const char* localName = stripNamespace(name);

    if (strcmp(localName, "emphasis") == 0 || strcmp(localName, "code") == 0) {
      self->italicUntilDepth_ = std::min(self->italicUntilDepth_, self->depth_);
    } else if (strcmp(localName, "strong") == 0) {
      self->boldUntilDepth_ = std::min(self->boldUntilDepth_, self->depth_);
    }

    self->depth_++;
  }

  static void XMLCALL endElement(void* userData, const XML_Char* /*name*/) {
    auto* self = static_cast<TestFb2StyleParser*>(userData);
    self->depth_--;
    if (self->depth_ == self->boldUntilDepth_) {
      self->boldUntilDepth_ = INT_MAX;
    }
    if (self->depth_ == self->italicUntilDepth_) {
      self->italicUntilDepth_ = INT_MAX;
    }
  }

  static void XMLCALL characterData(void* userData, const XML_Char* s, int len) {
    auto* self = static_cast<TestFb2StyleParser*>(userData);
    const bool bold = (self->boldUntilDepth_ < INT_MAX);
    const bool italic = (self->italicUntilDepth_ < INT_MAX);

    if (!self->runs.empty() && self->runs.back().bold == bold && self->runs.back().italic == italic) {
      self->runs.back().text.append(s, len);
    } else {
      self->runs.push_back({std::string(s, len), bold, italic});
    }
  }

 public:
  std::vector<StyledRun> runs;

  bool parse(const std::string& xml) {
    size_t bomSkip = 0;
    const char* explicitEncoding = nullptr;
    if (!xml.empty() && detectEncoding(reinterpret_cast<const uint8_t*>(xml.data()), xml.size(), bomSkip) ==
                            Encoding::Utf8) {
      explicitEncoding = "UTF-8";
    }

    XML_Parser parser = XML_ParserCreate(explicitEncoding);
    if (!parser) return false;

    XML_SetUserData(parser, this);
    XML_SetUnknownEncodingHandler(parser, expatUnknownEncodingHandler, nullptr);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterData);

    const char* data = xml.data() + bomSkip;
    const int len = static_cast<int>(xml.size() - bomSkip);
    const bool ok = XML_Parse(parser, data, len, 1) != XML_STATUS_ERROR;
    XML_ParserFree(parser);
    return ok;
  }
};

static std::string wrap(const std::string& bodyContent) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
         "<body><section><p>" +
         bodyContent +
         "</p></section></body>"
         "</FictionBook>";
}

// Find the first run whose text contains `needle`.
static const StyledRun* findRun(const std::vector<StyledRun>& runs, const std::string& needle) {
  for (const auto& r : runs) {
    if (r.text.find(needle) != std::string::npos) return &r;
  }
  return nullptr;
}

int main() {
  TestUtils::TestRunner runner("Fb2 Inline Styles");

  // Test 1: <emphasis> marks text as italic, not bold
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(wrap("<emphasis>foo</emphasis>"));
    runner.expectTrue(ok, "emphasis_italic: parses");
    const StyledRun* r = findRun(parser.runs, "foo");
    runner.expectTrue(r != nullptr, "emphasis_italic: run found");
    if (r) {
      runner.expectTrue(r->italic, "emphasis_italic: italic set");
      runner.expectFalse(r->bold, "emphasis_italic: bold not set");
    }
  }

  // Test 2: <code> marks text as italic (mirrors emphasis)
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(wrap("<code>bar</code>"));
    runner.expectTrue(ok, "code_italic: parses");
    const StyledRun* r = findRun(parser.runs, "bar");
    runner.expectTrue(r != nullptr, "code_italic: run found");
    if (r) {
      runner.expectTrue(r->italic, "code_italic: italic set");
      runner.expectFalse(r->bold, "code_italic: bold not set");
    }
  }

  // Test 3: <strong> marks text as bold, not italic
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(wrap("<strong>baz</strong>"));
    runner.expectTrue(ok, "strong_bold: parses");
    const StyledRun* r = findRun(parser.runs, "baz");
    runner.expectTrue(r != nullptr, "strong_bold: run found");
    if (r) {
      runner.expectTrue(r->bold, "strong_bold: bold set");
      runner.expectFalse(r->italic, "strong_bold: italic not set");
    }
  }

  // Test 4: <code> nested inside <strong> -> bold AND italic for inner run
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(wrap("<strong>a <code>b</code> c</strong>"));
    runner.expectTrue(ok, "code_nested_in_strong: parses");
    const StyledRun* inner = findRun(parser.runs, "b");
    runner.expectTrue(inner != nullptr, "code_nested_in_strong: inner run found");
    if (inner) {
      runner.expectTrue(inner->bold && inner->italic, "code_nested_in_strong: inner is bold+italic");
    }
    const StyledRun* outerA = findRun(parser.runs, "a ");
    runner.expectTrue(outerA != nullptr, "code_nested_in_strong: outer 'a' run found");
    if (outerA) {
      runner.expectTrue(outerA->bold, "code_nested_in_strong: outer bold");
      runner.expectFalse(outerA->italic, "code_nested_in_strong: outer not italic");
    }
    const StyledRun* outerC = findRun(parser.runs, " c");
    runner.expectTrue(outerC != nullptr, "code_nested_in_strong: trailing ' c' run found");
    if (outerC) {
      runner.expectTrue(outerC->bold, "code_nested_in_strong: trailing still bold after </code>");
      runner.expectFalse(outerC->italic, "code_nested_in_strong: trailing not italic");
    }
  }

  // Test 5: Italic scope closes when </code> ends
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(wrap("<code>x</code>y"));
    runner.expectTrue(ok, "code_closes_scope: parses");
    const StyledRun* rx = findRun(parser.runs, "x");
    const StyledRun* ry = findRun(parser.runs, "y");
    runner.expectTrue(rx != nullptr && ry != nullptr, "code_closes_scope: both runs found");
    if (rx) runner.expectTrue(rx->italic, "code_closes_scope: x italic");
    if (ry) runner.expectFalse(ry->italic, "code_closes_scope: y not italic");
  }

  // Test 6: <emphasis> and <code> produce equivalent italic styling
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(wrap("<emphasis>a</emphasis><code>b</code>"));
    runner.expectTrue(ok, "emphasis_and_code_equivalent: parses");
    const StyledRun* ra = findRun(parser.runs, "a");
    const StyledRun* rb = findRun(parser.runs, "b");
    runner.expectTrue(ra != nullptr && rb != nullptr, "emphasis_and_code_equivalent: both runs found");
    if (ra && rb) {
      runner.expectTrue(ra->italic && rb->italic, "emphasis_and_code_equivalent: both italic");
      runner.expectFalse(ra->bold || rb->bold, "emphasis_and_code_equivalent: neither bold");
    }
  }

  return runner.allPassed() ? 0 : 1;
}
