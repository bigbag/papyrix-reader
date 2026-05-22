#include "test_utils.h"

#include <string>
#include <vector>

// Simulates PlainTextParser's newline-reflow logic.
// Tracks paragraph boundaries: single \n = word separator (same paragraph),
// \n\n = paragraph break. Whitespace between \n chars is absorbed.
class ReflowHarness {
 public:
  std::vector<std::vector<std::string>> paragraphs;

  void parse(const char* text) {
    std::vector<std::string> currentParagraph;
    std::string word;
    bool sawNewline = false;

    for (const char* p = text; *p; ++p) {
      char c = *p;

      if (c == '\r') continue;

      if (c == '\n') {
        if (!word.empty()) {
          currentParagraph.push_back(word);
          word.clear();
        }

        if (sawNewline) {
          if (!currentParagraph.empty()) {
            paragraphs.push_back(currentParagraph);
            currentParagraph.clear();
          }
        } else {
          sawNewline = true;
        }
        continue;
      }

      if (c == ' ' || c == '\t') {
        if (!word.empty()) {
          currentParagraph.push_back(word);
          word.clear();
        }
        continue;
      }

      sawNewline = false;
      word += c;
    }

    if (!word.empty()) {
      currentParagraph.push_back(word);
    }
    if (!currentParagraph.empty()) {
      paragraphs.push_back(currentParagraph);
    }
  }
};

int main() {
  TestUtils::TestRunner runner("TXT Reflow");

  // Test 1: Single newline = continuation (same paragraph)
  {
    ReflowHarness h;
    h.parse("hello\nworld");
    runner.expectEq(1, static_cast<int>(h.paragraphs.size()),"single_newline: 1 paragraph");
    runner.expectEq(2, static_cast<int>(h.paragraphs[0].size()),"single_newline: 2 words");
    runner.expectEqual(std::string("hello"), h.paragraphs[0][0], "single_newline: word 1");
    runner.expectEqual(std::string("world"), h.paragraphs[0][1], "single_newline: word 2");
  }

  // Test 2: Double newline = paragraph break
  {
    ReflowHarness h;
    h.parse("hello\n\nworld");
    runner.expectEq(2, static_cast<int>(h.paragraphs.size()),"double_newline: 2 paragraphs");
    runner.expectEqual(std::string("hello"), h.paragraphs[0][0], "double_newline: para 1");
    runner.expectEqual(std::string("world"), h.paragraphs[1][0], "double_newline: para 2");
  }

  // Test 3: CRLF = continuation
  {
    ReflowHarness h;
    h.parse("hello\r\nworld");
    runner.expectEq(1, static_cast<int>(h.paragraphs.size()),"crlf: 1 paragraph");
    runner.expectEq(2, static_cast<int>(h.paragraphs[0].size()),"crlf: 2 words");
  }

  // Test 4: CRLF double = paragraph break
  {
    ReflowHarness h;
    h.parse("hello\r\n\r\nworld");
    runner.expectEq(2, static_cast<int>(h.paragraphs.size()),"crlf_double: 2 paragraphs");
  }

  // Test 5: Blank line with spaces = paragraph break
  {
    ReflowHarness h;
    h.parse("hello\n   \nworld");
    runner.expectEq(2, static_cast<int>(h.paragraphs.size()),"blank_with_spaces: 2 paragraphs");
  }

  // Test 6: Triple newline = single paragraph break
  {
    ReflowHarness h;
    h.parse("hello\n\n\nworld");
    runner.expectEq(2, static_cast<int>(h.paragraphs.size()),"triple_newline: 2 paragraphs");
  }

  // Test 7: Indented continuation (leading whitespace after single \n)
  {
    ReflowHarness h;
    h.parse("hello\n  world");
    runner.expectEq(1, static_cast<int>(h.paragraphs.size()),"indented_continuation: 1 paragraph");
    runner.expectEq(2, static_cast<int>(h.paragraphs[0].size()),"indented_continuation: 2 words");
  }

  // Test 8: Trailing newline
  {
    ReflowHarness h;
    h.parse("hello\n");
    runner.expectEq(1, static_cast<int>(h.paragraphs.size()),"trailing_newline: 1 paragraph");
    runner.expectEqual(std::string("hello"), h.paragraphs[0][0], "trailing_newline: word");
  }

  // Test 9: Multiple paragraphs
  {
    ReflowHarness h;
    h.parse("a\n\nb\n\nc");
    runner.expectEq(3, static_cast<int>(h.paragraphs.size()),"multi_para: 3 paragraphs");
    runner.expectEqual(std::string("a"), h.paragraphs[0][0], "multi_para: para 1");
    runner.expectEqual(std::string("b"), h.paragraphs[1][0], "multi_para: para 2");
    runner.expectEqual(std::string("c"), h.paragraphs[2][0], "multi_para: para 3");
  }

  // Test 10: Tab after newline = continuation
  {
    ReflowHarness h;
    h.parse("hello\n\tworld");
    runner.expectEq(1, static_cast<int>(h.paragraphs.size()),"tab_continuation: 1 paragraph");
    runner.expectEq(2, static_cast<int>(h.paragraphs[0].size()),"tab_continuation: 2 words");
  }

  // Test 11: Empty input
  {
    ReflowHarness h;
    h.parse("");
    runner.expectEq(0, static_cast<int>(h.paragraphs.size()),"empty: 0 paragraphs");
  }

  // Test 12: Only whitespace/newlines
  {
    ReflowHarness h;
    h.parse("\n\n");
    runner.expectEq(0, static_cast<int>(h.paragraphs.size()),"only_newlines: 0 paragraphs");
  }

  // Test 13: Gutenberg-style fixed-width text
  {
    ReflowHarness h;
    h.parse(
        "The Project Gutenberg eBook of The Declaration of Independence\n"
        "of the United States of America\n"
        "\n"
        "This eBook is for the use of anyone anywhere in the United\n"
        "States and most other parts of the world at no cost.");
    runner.expectEq(2, static_cast<int>(h.paragraphs.size()),"gutenberg: 2 paragraphs");
    runner.expectTrue(h.paragraphs[0].size() > 5, "gutenberg: para 1 has many words");
    runner.expectTrue(h.paragraphs[1].size() > 5, "gutenberg: para 2 has many words");
  }

  // Test 14: Mixed CRLF with spaces between blank lines
  {
    ReflowHarness h;
    h.parse("line1\r\n  \r\nline2");
    runner.expectEq(2, static_cast<int>(h.paragraphs.size()),"crlf_blank_spaces: 2 paragraphs");
  }

  return runner.allPassed() ? 0 : 1;
}
