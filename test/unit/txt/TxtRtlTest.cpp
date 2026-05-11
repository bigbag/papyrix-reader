#include "test_utils.h"

#include <ScriptDetector.h>

#include <cstring>
#include <string>
#include <vector>

// Simulates PlainTextParser's word splitting + RTL detection pattern.
// Splits on whitespace, calls ScriptDetector::classify per word.
class TxtRtlHarness {
  bool isRtl_ = false;

  void addWord(const char* word) {
    if (!isRtl_ && ScriptDetector::classify(word) == ScriptDetector::Script::ARABIC) {
      isRtl_ = true;
    }
    words.push_back(word);
  }

 public:
  std::vector<std::string> words;
  bool isRtl() const { return isRtl_; }

  void parse(const char* text) {
    std::string word;
    for (const char* p = text; *p; ++p) {
      if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        if (!word.empty()) {
          addWord(word.c_str());
          word.clear();
        }
      } else {
        word += *p;
      }
    }
    if (!word.empty()) {
      addWord(word.c_str());
    }
  }
};

int main() {
  TestUtils::TestRunner runner("TXT RTL Detection");

  // Test 1: ASCII text is LTR
  {
    TxtRtlHarness h;
    h.parse("Hello world this is plain text");
    runner.expectFalse(h.isRtl(), "ascii_ltr: ASCII text is LTR");
  }

  // Test 2: Russian text is LTR
  {
    TxtRtlHarness h;
    h.parse("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xd0\xbc\xd0\xb8\xd1\x80");
    runner.expectFalse(h.isRtl(), "russian_ltr: Russian text is LTR");
  }

  // Test 3: Arabic text sets RTL
  {
    TxtRtlHarness h;
    h.parse("\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85");
    runner.expectTrue(h.isRtl(), "arabic_rtl: Arabic text is RTL");
  }

  // Test 4: Mixed Latin then Arabic sets RTL
  {
    TxtRtlHarness h;
    h.parse("Hello world \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7");
    runner.expectTrue(h.isRtl(), "mixed_rtl: Latin followed by Arabic sets RTL");
  }

  // Test 5: Arabic at end of long text (peek-buffer would miss this)
  {
    TxtRtlHarness h;
    std::string longText;
    for (int i = 0; i < 200; i++) {
      longText += "word" + std::to_string(i) + " ";
    }
    longText += "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";
    h.parse(longText.c_str());
    runner.expectTrue(h.isRtl(), "late_arabic_rtl: Arabic after 200 Latin words sets RTL");
  }

  // Test 6: RTL flag is sticky
  {
    TxtRtlHarness h;
    h.parse("\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 hello world");
    runner.expectTrue(h.isRtl(), "sticky_rtl: RTL stays true after Arabic then Latin");
  }

  // Test 7: Empty input
  {
    TxtRtlHarness h;
    h.parse("");
    runner.expectFalse(h.isRtl(), "empty_ltr: empty text is LTR");
  }

  return runner.allPassed() ? 0 : 1;
}
