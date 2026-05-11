#include "test_utils.h"

#include <ScriptDetector.h>

#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "md_parser.h"
}

// Simulates MarkdownParser's flushWordBuffer + RTL detection pattern.
// Uses md_parser for tokenization, applies ScriptDetector::classify per word.
class MdRtlHarness {
  static constexpr int MAX_WORD_SIZE = 200;

  bool isRtl_ = false;
  char wordBuffer_[MAX_WORD_SIZE + 1] = {};
  int wordIndex_ = 0;

  void flushWordBuffer() {
    if (wordIndex_ == 0) return;
    wordBuffer_[wordIndex_] = '\0';
    if (!isRtl_ && ScriptDetector::classify(wordBuffer_) == ScriptDetector::Script::ARABIC) {
      isRtl_ = true;
    }
    words.push_back(wordBuffer_);
    wordIndex_ = 0;
  }

  static bool tokenCallback(const md_token_t* token, void* userData) {
    auto* self = static_cast<MdRtlHarness*>(userData);
    if (token->type == MD_TEXT) {
      for (uint16_t i = 0; i < token->length; i++) {
        char c = token->text[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
          self->flushWordBuffer();
        } else {
          if (self->wordIndex_ >= MAX_WORD_SIZE) {
            self->flushWordBuffer();
          }
          self->wordBuffer_[self->wordIndex_++] = c;
        }
      }
    }
    return true;
  }

 public:
  std::vector<std::string> words;
  bool isRtl() const { return isRtl_; }

  void parse(const char* markdown) {
    isRtl_ = false;
    wordIndex_ = 0;
    words.clear();

    md_parser_t parser;
    md_parser_init(&parser, tokenCallback, this);

    const char* line = markdown;
    while (*line) {
      const char* nl = strchr(line, '\n');
      int len = nl ? static_cast<int>(nl - line) : static_cast<int>(strlen(line));
      md_parser_reset(&parser);
      md_parse(&parser, line, len);
      line += len + (nl ? 1 : len);
      if (!nl) break;
    }
    flushWordBuffer();
  }
};

int main() {
  TestUtils::TestRunner runner("Markdown RTL Detection");

  // Test 1: Plain ASCII markdown is LTR
  {
    MdRtlHarness h;
    h.parse("Hello **bold** world");
    runner.expectFalse(h.isRtl(), "ascii_ltr: ASCII markdown is LTR");
  }

  // Test 2: Arabic text in markdown sets RTL
  {
    MdRtlHarness h;
    h.parse("\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85");
    runner.expectTrue(h.isRtl(), "arabic_rtl: Arabic markdown is RTL");
  }

  // Test 3: Arabic inside bold formatting
  {
    MdRtlHarness h;
    h.parse("**\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7**");
    runner.expectTrue(h.isRtl(), "arabic_bold_rtl: bold Arabic sets RTL");
  }

  // Test 4: Latin header then Arabic body
  {
    MdRtlHarness h;
    h.parse("# Title\n\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85");
    runner.expectTrue(h.isRtl(), "header_then_arabic: Arabic after Latin header sets RTL");
  }

  // Test 5: Russian text is LTR
  {
    MdRtlHarness h;
    h.parse("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xd0\xbc\xd0\xb8\xd1\x80");
    runner.expectFalse(h.isRtl(), "russian_ltr: Russian markdown is LTR");
  }

  // Test 6: RTL flag is sticky across lines
  {
    MdRtlHarness h;
    h.parse("\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7\nhello world");
    runner.expectTrue(h.isRtl(), "sticky_rtl: RTL sticky across lines");
  }

  // Test 7: Empty markdown
  {
    MdRtlHarness h;
    h.parse("");
    runner.expectFalse(h.isRtl(), "empty_ltr: empty markdown is LTR");
  }

  return runner.allPassed() ? 0 : 1;
}
