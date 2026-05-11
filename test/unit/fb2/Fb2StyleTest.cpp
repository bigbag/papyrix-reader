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
#include <ScriptDetector.h>
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

static std::string wrapSection(const std::string& sectionContent) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
         "<body><section>" +
         sectionContent +
         "</section></body>"
         "</FictionBook>";
}

// Find the first run whose text contains `needle`.
static const StyledRun* findRun(const std::vector<StyledRun>& runs, const std::string& needle) {
  for (const auto& r : runs) {
    if (r.text.find(needle) != std::string::npos) return &r;
  }
  return nullptr;
}

// Word-buffering harness that mirrors production Fb2Parser's partWordBuffer/flush behavior.
// Unlike TestFb2StyleParser (which captures style per characterData callback), this harness
// only assigns style at flush time, reproducing the bug where the last word in a style tag
// loses formatting.
class TestFb2WordFlushParser {
  static constexpr int MAX_WORD_SIZE = 128;

  int depth_ = 0;
  int boldUntilDepth_ = INT_MAX;
  int italicUntilDepth_ = INT_MAX;
  bool isRtl_ = false;
  char wordBuffer_[MAX_WORD_SIZE + 1] = {};
  int wordIndex_ = 0;

  static const char* stripNamespace(const XML_Char* name) {
    const char* tag = strrchr(name, ':');
    return tag ? tag + 1 : name;
  }

  void flushWordBuffer() {
    if (wordIndex_ == 0) return;
    wordBuffer_[wordIndex_] = '\0';
    bool bold = (boldUntilDepth_ < INT_MAX);
    bool italic = (italicUntilDepth_ < INT_MAX);
    if (!isRtl_ && ScriptDetector::classify(wordBuffer_) == ScriptDetector::Script::ARABIC) {
      isRtl_ = true;
    }
    words.push_back({std::string(wordBuffer_), bold, italic});
    wordIndex_ = 0;
  }

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char**) {
    auto* self = static_cast<TestFb2WordFlushParser*>(userData);
    const char* localName = stripNamespace(name);

    if (strcmp(localName, "emphasis") == 0 || strcmp(localName, "code") == 0) {
      self->flushWordBuffer();
      self->italicUntilDepth_ = std::min(self->italicUntilDepth_, self->depth_);
    } else if (strcmp(localName, "strong") == 0) {
      self->flushWordBuffer();
      self->boldUntilDepth_ = std::min(self->boldUntilDepth_, self->depth_);
    }
    self->depth_++;
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<TestFb2WordFlushParser*>(userData);
    const char* localName = stripNamespace(name);

    if (strcmp(localName, "emphasis") == 0 || strcmp(localName, "code") == 0 ||
        strcmp(localName, "strong") == 0) {
      self->flushWordBuffer();
    }

    self->depth_--;
    if (self->depth_ <= self->boldUntilDepth_) self->boldUntilDepth_ = INT_MAX;
    if (self->depth_ <= self->italicUntilDepth_) self->italicUntilDepth_ = INT_MAX;

    if (strcmp(localName, "p") == 0) self->flushWordBuffer();
  }

  static void XMLCALL characterData(void* userData, const XML_Char* s, int len) {
    auto* self = static_cast<TestFb2WordFlushParser*>(userData);
    for (int i = 0; i < len; i++) {
      if (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') {
        self->flushWordBuffer();
        continue;
      }
      if (self->wordIndex_ < MAX_WORD_SIZE) {
        self->wordBuffer_[self->wordIndex_++] = s[i];
      }
    }
  }

 public:
  std::vector<StyledRun> words;
  bool isRtl() const { return isRtl_; }

  bool parse(const std::string& xml) {
    XML_Parser parser = XML_ParserCreate("UTF-8");
    if (!parser) return false;
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterData);
    const bool ok = XML_Parse(parser, xml.data(), static_cast<int>(xml.size()), 1) != XML_STATUS_ERROR;
    XML_ParserFree(parser);
    return ok;
  }
};

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

  // Test 7: Text inside <v> (verse line) is captured
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(wrapSection("<poem><stanza><v>hello world</v></stanza></poem>"));
    runner.expectTrue(ok, "verse_text_captured: parses");
    const StyledRun* r = findRun(parser.runs, "hello");
    runner.expectTrue(r != nullptr, "verse_text_captured: verse text found");
  }

  // Test 8: Emphasis inside <v> produces italic
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(wrapSection("<poem><stanza><v><emphasis>italic verse</emphasis></v></stanza></poem>"));
    runner.expectTrue(ok, "verse_italic_emphasis: parses");
    const StyledRun* r = findRun(parser.runs, "italic verse");
    runner.expectTrue(r != nullptr, "verse_italic_emphasis: run found");
    if (r) {
      runner.expectTrue(r->italic, "verse_italic_emphasis: italic set");
    }
  }

  // Test 9: Text inside <text-author> is captured
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(
        wrapSection("<poem><stanza><v>line</v></stanza><text-author>Author Name</text-author></poem>"));
    runner.expectTrue(ok, "text_author_captured: parses");
    const StyledRun* r = findRun(parser.runs, "Author");
    runner.expectTrue(r != nullptr, "text_author_captured: text-author text found");
  }

  // Test 10: Multiple <v> elements both captured
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(wrapSection("<poem><stanza><v>first</v><v>second</v></stanza></poem>"));
    runner.expectTrue(ok, "multiple_verses: parses");
    const StyledRun* r1 = findRun(parser.runs, "first");
    const StyledRun* r2 = findRun(parser.runs, "second");
    runner.expectTrue(r1 != nullptr, "multiple_verses: first verse found");
    runner.expectTrue(r2 != nullptr, "multiple_verses: second verse found");
  }

  // Test 11: Strong inside <v> produces bold
  {
    TestFb2StyleParser parser;
    bool ok = parser.parse(wrapSection("<poem><stanza><v><strong>bold verse</strong></v></stanza></poem>"));
    runner.expectTrue(ok, "verse_strong_bold: parses");
    const StyledRun* r = findRun(parser.runs, "bold verse");
    runner.expectTrue(r != nullptr, "verse_strong_bold: run found");
    if (r) {
      runner.expectTrue(r->bold, "verse_strong_bold: bold set");
      runner.expectFalse(r->italic, "verse_strong_bold: italic not set");
    }
  }

  // --- Word-flush tests (reproduce issue #114: last word loses style) ---

  // Test 12: Last word in <emphasis> retains italic
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("<emphasis>foo bar</emphasis>"));
    runner.expectTrue(ok, "flush_emphasis_last_word: parses");
    const StyledRun* r = findRun(p.words, "bar");
    runner.expectTrue(r != nullptr, "flush_emphasis_last_word: 'bar' found");
    if (r) runner.expectTrue(r->italic, "flush_emphasis_last_word: 'bar' is italic");
  }

  // Test 13: Last word in <strong> retains bold
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("<strong>foo bar</strong>"));
    runner.expectTrue(ok, "flush_strong_last_word: parses");
    const StyledRun* r = findRun(p.words, "bar");
    runner.expectTrue(r != nullptr, "flush_strong_last_word: 'bar' found");
    if (r) runner.expectTrue(r->bold, "flush_strong_last_word: 'bar' is bold");
  }

  // Test 14: Last word in <code> retains italic
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("<code>foo bar</code>"));
    runner.expectTrue(ok, "flush_code_last_word: parses");
    const StyledRun* r = findRun(p.words, "bar");
    runner.expectTrue(r != nullptr, "flush_code_last_word: 'bar' found");
    if (r) runner.expectTrue(r->italic, "flush_code_last_word: 'bar' is italic");
  }

  // Test 15: Single word with punctuation in <emphasis> is italic
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("<emphasis>word.</emphasis>"));
    runner.expectTrue(ok, "flush_emphasis_single_word: parses");
    const StyledRun* r = findRun(p.words, "word.");
    runner.expectTrue(r != nullptr, "flush_emphasis_single_word: 'word.' found");
    if (r) runner.expectTrue(r->italic, "flush_emphasis_single_word: 'word.' is italic");
  }

  // Test 16: Nested <emphasis> inside <strong> — inner word is bold+italic
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("<strong>a <emphasis>b</emphasis> c</strong>"));
    runner.expectTrue(ok, "flush_nested: parses");
    const StyledRun* rb = findRun(p.words, "b");
    runner.expectTrue(rb != nullptr, "flush_nested: 'b' found");
    if (rb) runner.expectTrue(rb->bold && rb->italic, "flush_nested: 'b' is bold+italic");
    const StyledRun* rc = findRun(p.words, "c");
    runner.expectTrue(rc != nullptr, "flush_nested: 'c' found");
    if (rc) {
      runner.expectTrue(rc->bold, "flush_nested: 'c' is bold");
      runner.expectFalse(rc->italic, "flush_nested: 'c' is not italic");
    }
  }

  // Test 17: Opening boundary — word before <emphasis> stays regular
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("word<emphasis>italic</emphasis>"));
    runner.expectTrue(ok, "flush_opening_boundary: parses");
    const StyledRun* rw = findRun(p.words, "word");
    runner.expectTrue(rw != nullptr, "flush_opening_boundary: 'word' found");
    if (rw) runner.expectFalse(rw->italic, "flush_opening_boundary: 'word' is not italic");
    const StyledRun* ri = findRun(p.words, "italic");
    runner.expectTrue(ri != nullptr, "flush_opening_boundary: 'italic' found");
    if (ri) runner.expectTrue(ri->italic, "flush_opening_boundary: 'italic' is italic");
  }

  // Test 18: Word after closing </emphasis> is regular
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("<emphasis>italic</emphasis> normal"));
    runner.expectTrue(ok, "flush_after_close: parses");
    const StyledRun* ri = findRun(p.words, "italic");
    runner.expectTrue(ri != nullptr, "flush_after_close: 'italic' found");
    if (ri) runner.expectTrue(ri->italic, "flush_after_close: 'italic' is italic");
    const StyledRun* rn = findRun(p.words, "normal");
    runner.expectTrue(rn != nullptr, "flush_after_close: 'normal' found");
    if (rn) runner.expectFalse(rn->italic, "flush_after_close: 'normal' is not italic");
  }

  // --- RTL detection tests (word-based, mirrors flushPartWordBuffer fix) ---

  // Test 19: Russian text is not RTL
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xd0\xbc\xd0\xb8\xd1\x80"));
    runner.expectTrue(ok, "rtl_russian_not_rtl: parses");
    runner.expectFalse(p.isRtl(), "rtl_russian_not_rtl: Russian text is LTR");
  }

  // Test 20: Arabic text sets RTL
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85"));
    runner.expectTrue(ok, "rtl_arabic_is_rtl: parses");
    runner.expectTrue(p.isRtl(), "rtl_arabic_is_rtl: Arabic text is RTL");
  }

  // Test 21: Pure ASCII text is not RTL
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("Hello world"));
    runner.expectTrue(ok, "rtl_ascii_not_rtl: parses");
    runner.expectFalse(p.isRtl(), "rtl_ascii_not_rtl: ASCII text is LTR");
  }

  // Test 22: Mixed Latin then Arabic sets RTL
  {
    TestFb2WordFlushParser p;
    bool ok = p.parse(wrap("Hello \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7"));
    runner.expectTrue(ok, "rtl_mixed_sets_rtl: parses");
    runner.expectTrue(p.isRtl(), "rtl_mixed_sets_rtl: mixed text with Arabic is RTL");
  }

  return runner.allPassed() ? 0 : 1;
}
