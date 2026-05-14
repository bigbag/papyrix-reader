#include "test_utils.h"

#include <GfxRenderer.h>
#include <ParsedText.h>

#include <memory>
#include <string>
#include <vector>

// Collect words from extracted TextBlock lines
static std::vector<std::string> collectWords(const std::vector<std::shared_ptr<TextBlock>>& lines) {
  std::vector<std::string> result;
  for (auto& line : lines) {
    for (auto& wd : line->getWords()) {
      result.push_back(wd.word);
    }
  }
  return result;
}

// Metrics:  6px per char, 4px space.
// Viewport: 60px → 2 words of 3 chars per line.
//   "aaa" = 18px.  "aaa bbb" = 18+4+18 = 40px fits.
//   "aaa bbb ccc" = 40+4+18 = 62px wraps → "ccc" goes to line 2.
static constexpr uint16_t kViewport = 60;
static constexpr int kFontId = 1;

static std::unique_ptr<ParsedText> makeBlock(int wordCount) {
  auto pt = std::make_unique<ParsedText>(TextBlock::LEFT_ALIGN, 0, false, true, false);
  const char* names[] = {"aaa", "bbb", "ccc", "ddd", "eee", "fff", "ggg", "hhh", "iii", "jjj"};
  for (int i = 0; i < wordCount && i < 10; i++) {
    pt->addWord(names[i], EpdFontFamily::REGULAR);
  }
  return pt;
}

int main() {
  TestUtils::TestRunner runner("ParsedTextAbort");
  GfxRenderer renderer;

  // --- Empty ParsedText returns true immediately ---
  {
    ParsedText pt(TextBlock::LEFT_ALIGN, 0, false);
    std::vector<std::shared_ptr<TextBlock>> lines;
    bool ok = pt.layoutAndExtractLines(renderer, kFontId, kViewport,
                                       [&](std::shared_ptr<TextBlock> l) { lines.push_back(l); });
    runner.expectTrue(ok, "empty_returns_true");
    runner.expectEq(size_t(0), lines.size(), "empty_no_lines");
  }

  // --- No abort: all words consumed ---
  {
    auto pt = makeBlock(6);  // 6 words → 3 lines (2 per line)
    std::vector<std::shared_ptr<TextBlock>> lines;
    bool ok = pt->layoutAndExtractLines(renderer, kFontId, kViewport,
                                        [&](std::shared_ptr<TextBlock> l) { lines.push_back(l); });
    runner.expectTrue(ok, "no_abort_returns_true");
    runner.expectEq(size_t(3), lines.size(), "no_abort_three_lines");
    runner.expectTrue(pt->isEmpty(), "no_abort_all_consumed");
  }

  // --- Abort before any work: words fully preserved ---
  {
    auto pt = makeBlock(6);
    std::vector<std::shared_ptr<TextBlock>> lines;
    bool ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport, [&](std::shared_ptr<TextBlock> l) { lines.push_back(l); }, true,
        []() -> bool { return true; });
    runner.expectFalse(ok, "immediate_abort_returns_false");
    runner.expectEq(size_t(0), lines.size(), "immediate_abort_no_lines");
    runner.expectEq(size_t(6), pt->size(), "immediate_abort_words_preserved");
  }

  // --- Abort after 1 line: remaining words preserved ---
  {
    auto pt = makeBlock(6);  // 3 lines of 2 words each
    std::vector<std::shared_ptr<TextBlock>> lines;
    int linesCollected = 0;

    bool ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport,
        [&](std::shared_ptr<TextBlock> l) {
          lines.push_back(l);
          linesCollected++;
        },
        true, [&]() -> bool { return linesCollected >= 1; });

    runner.expectFalse(ok, "abort_after_1_returns_false");
    runner.expectEq(size_t(1), lines.size(), "abort_after_1_one_line");
    runner.expectEq(size_t(4), pt->size(), "abort_after_1_four_words_remain");
  }

  // --- Abort after 2 lines: 1 line of words remain ---
  {
    auto pt = makeBlock(6);
    std::vector<std::shared_ptr<TextBlock>> lines;
    int linesCollected = 0;

    bool ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport,
        [&](std::shared_ptr<TextBlock> l) {
          lines.push_back(l);
          linesCollected++;
        },
        true, [&]() -> bool { return linesCollected >= 2; });

    runner.expectFalse(ok, "abort_after_2_returns_false");
    runner.expectEq(size_t(2), lines.size(), "abort_after_2_two_lines");
    runner.expectEq(size_t(2), pt->size(), "abort_after_2_two_words_remain");
  }

  // --- includeLastLine=false: last line's words preserved ---
  {
    auto pt = makeBlock(6);
    std::vector<std::shared_ptr<TextBlock>> lines;

    bool ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport, [&](std::shared_ptr<TextBlock> l) { lines.push_back(l); }, false);

    runner.expectTrue(ok, "exclude_last_returns_true");
    runner.expectEq(size_t(2), lines.size(), "exclude_last_two_lines");
    runner.expectEq(size_t(2), pt->size(), "exclude_last_two_words_remain");
  }

  // --- includeLastLine=true: all lines consumed ---
  {
    auto pt = makeBlock(6);
    std::vector<std::shared_ptr<TextBlock>> lines;

    bool ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport, [&](std::shared_ptr<TextBlock> l) { lines.push_back(l); }, true);

    runner.expectTrue(ok, "include_last_returns_true");
    runner.expectEq(size_t(3), lines.size(), "include_last_three_lines");
    runner.expectTrue(pt->isEmpty(), "include_last_all_consumed");
  }

  // --- Re-call after abort: remaining words processed correctly ---
  {
    auto pt = makeBlock(6);  // 6 words → 3 lines
    std::vector<std::shared_ptr<TextBlock>> firstBatch, secondBatch;
    int linesCollected = 0;

    // First call: extract 1 line, then abort
    pt->layoutAndExtractLines(
        renderer, kFontId, kViewport,
        [&](std::shared_ptr<TextBlock> l) {
          firstBatch.push_back(l);
          linesCollected++;
        },
        true, [&]() -> bool { return linesCollected >= 1; });

    runner.expectEq(size_t(1), firstBatch.size(), "resume_first_batch_one_line");
    runner.expectFalse(pt->isEmpty(), "resume_words_remain_after_first");

    // Second call: no abort, extract remaining
    bool ok = pt->layoutAndExtractLines(renderer, kFontId, kViewport,
                                        [&](std::shared_ptr<TextBlock> l) { secondBatch.push_back(l); });

    runner.expectTrue(ok, "resume_second_returns_true");
    runner.expectEq(size_t(2), secondBatch.size(), "resume_second_batch_two_lines");
    runner.expectTrue(pt->isEmpty(), "resume_all_consumed_after_second");

    // Verify total words across both batches
    auto w1 = collectWords(firstBatch);
    auto w2 = collectWords(secondBatch);
    runner.expectEq(size_t(6), w1.size() + w2.size(), "resume_total_words_match");
  }

  // --- Multi-step resume: 3 batches of 1 line each ---
  {
    auto pt = makeBlock(6);
    size_t totalWords = 0;

    for (int batch = 0; batch < 3; batch++) {
      std::vector<std::shared_ptr<TextBlock>> lines;
      int linesCollected = 0;

      pt->layoutAndExtractLines(
          renderer, kFontId, kViewport,
          [&](std::shared_ptr<TextBlock> l) {
            lines.push_back(l);
            linesCollected++;
          },
          true, [&]() -> bool { return linesCollected >= 1; });

      for (auto& line : lines) {
        totalWords += line->getWords().size();
      }
    }

    runner.expectTrue(pt->isEmpty(), "multi_resume_all_consumed");
    runner.expectEq(size_t(6), totalWords, "multi_resume_total_words");
  }

  // --- Simulates parser pattern: processLine sets abort flag ---
  {
    auto pt = makeBlock(8);  // 8 words → 4 lines
    std::vector<std::shared_ptr<TextBlock>> lines;
    bool hitMax = false;

    bool ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport,
        [&](std::shared_ptr<TextBlock> l) {
          if (!hitMax) {
            lines.push_back(l);
            if (lines.size() >= 2) {
              hitMax = true;
            }
          }
        },
        true, [&]() -> bool { return hitMax; });

    runner.expectFalse(ok, "parser_pattern_returns_false");
    runner.expectEq(size_t(2), lines.size(), "parser_pattern_two_lines");
    runner.expectFalse(pt->isEmpty(), "parser_pattern_words_remain");

    // Remaining words can be extracted in next call
    std::vector<std::shared_ptr<TextBlock>> rest;
    bool ok2 = pt->layoutAndExtractLines(renderer, kFontId, kViewport,
                                         [&](std::shared_ptr<TextBlock> l) { rest.push_back(l); });
    runner.expectTrue(ok2, "parser_pattern_resume_ok");
    runner.expectTrue(pt->isEmpty(), "parser_pattern_resume_consumed");

    auto w1 = collectWords(lines);
    auto w2 = collectWords(rest);
    runner.expectEq(size_t(8), w1.size() + w2.size(), "parser_pattern_all_words_accounted");
  }

  // --- Single word: always fits on one line ---
  {
    auto pt = makeBlock(1);
    std::vector<std::shared_ptr<TextBlock>> lines;
    bool ok = pt->layoutAndExtractLines(renderer, kFontId, kViewport,
                                        [&](std::shared_ptr<TextBlock> l) { lines.push_back(l); });
    runner.expectTrue(ok, "single_word_ok");
    runner.expectEq(size_t(1), lines.size(), "single_word_one_line");
    runner.expectTrue(pt->isEmpty(), "single_word_consumed");
  }

  // --- includeLastLine=false with single line: no lines extracted ---
  {
    auto pt = makeBlock(2);  // 2 words fit in 1 line
    std::vector<std::shared_ptr<TextBlock>> lines;
    bool ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport, [&](std::shared_ptr<TextBlock> l) { lines.push_back(l); }, false);
    runner.expectTrue(ok, "exclude_last_single_line_ok");
    runner.expectEq(size_t(0), lines.size(), "exclude_last_single_line_no_extract");
    runner.expectEq(size_t(2), pt->size(), "exclude_last_single_line_words_preserved");
  }

  // --- Indentation applied only once (includeLastLine=false resume) ---
  {
    auto pt = std::make_unique<ParsedText>(TextBlock::LEFT_ALIGN, 2, false, true, false);
    const char* names[] = {"aaa", "bbb", "ccc", "ddd", "eee", "fff"};
    for (int i = 0; i < 6; i++) {
      pt->addWord(names[i], EpdFontFamily::REGULAR);
    }
    std::vector<std::shared_ptr<TextBlock>> lines;
    bool ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport,
        [&](std::shared_ptr<TextBlock> l) { lines.push_back(l); }, false);
    runner.expectTrue(ok, "indent_resume_exclude_ok");
    runner.expectEq(size_t(2), lines.size(), "indent_resume_exclude_two_lines");

    auto& words1 = lines[0]->getWords();
    runner.expectTrue(words1.size() > 0, "indent_resume_first_line_has_words");
    if (words1.size() > 0) {
      runner.expectEqual(std::string("\xe2\x80\x83" "aaa"), words1[0].word, "indent_resume_first_word_indented");
    }

    std::vector<std::shared_ptr<TextBlock>> lines2;
    ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport,
        [&](std::shared_ptr<TextBlock> l) { lines2.push_back(l); }, true);
    runner.expectTrue(ok, "indent_resume_second_ok");
    runner.expectEq(size_t(1), lines2.size(), "indent_resume_second_one_line");

    auto& words2 = lines2[0]->getWords();
    runner.expectTrue(words2.size() > 0, "indent_resume_second_has_words");
    if (words2.size() > 0) {
      runner.expectEqual(std::string("eee"), words2[0].word, "indent_resume_no_double_indent");
    }
  }

  // --- Indentation not duplicated after abort ---
  {
    auto pt = std::make_unique<ParsedText>(TextBlock::LEFT_ALIGN, 2, false, true, false);
    const char* names[] = {"aaa", "bbb", "ccc", "ddd", "eee", "fff"};
    for (int i = 0; i < 6; i++) {
      pt->addWord(names[i], EpdFontFamily::REGULAR);
    }
    std::vector<std::shared_ptr<TextBlock>> lines;
    int linesCollected = 0;
    bool ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport,
        [&](std::shared_ptr<TextBlock> l) {
          lines.push_back(l);
          linesCollected++;
        },
        true, [&]() -> bool { return linesCollected >= 1; });
    runner.expectFalse(ok, "indent_abort_returns_false");
    runner.expectEq(size_t(1), lines.size(), "indent_abort_one_line");

    auto& words1 = lines[0]->getWords();
    runner.expectTrue(words1.size() > 0, "indent_abort_first_line_has_words");
    if (words1.size() > 0) {
      runner.expectEqual(std::string("\xe2\x80\x83" "aaa"), words1[0].word, "indent_abort_first_word_indented");
    }

    std::vector<std::shared_ptr<TextBlock>> lines2;
    ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport,
        [&](std::shared_ptr<TextBlock> l) { lines2.push_back(l); }, true);
    runner.expectTrue(ok, "indent_abort_resume_ok");
    runner.expectEq(size_t(2), lines2.size(), "indent_abort_resume_two_lines");

    auto& words2 = lines2[0]->getWords();
    runner.expectTrue(words2.size() > 0, "indent_abort_resume_first_has_words");
    if (words2.size() > 0) {
      runner.expectEqual(std::string("ccc"), words2[0].word, "indent_abort_resume_no_double_indent");
    }
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
