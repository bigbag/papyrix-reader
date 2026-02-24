#include "ParsedText.h"

#include <GfxRenderer.h>
#include <Hyphenation.h>
#include <Logging.h>
#include <Utf8.h>

#define TAG "TEXT"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

// Knuth-Plass algorithm constants
constexpr float INFINITY_PENALTY = 10000.0f;
constexpr float LINE_PENALTY = 50.0f;

// Soft hyphen (U+00AD) as UTF-8 bytes
constexpr unsigned char SOFT_HYPHEN_BYTE1 = 0xC2;
constexpr unsigned char SOFT_HYPHEN_BYTE2 = 0xAD;

// Known attaching punctuation (including UTF-8 sequences)
const std::vector<std::string> punctuation = {
    ".",
    ",",
    "!",
    "?",
    ";",
    ":",
    "\"",
    "'",
    "\xE2\x80\x99",  // ' (U+2019 right single quote)
    "\xE2\x80\x9D"   // " (U+201D right double quote)
};

// Check if a word consists entirely of attaching punctuation
// These should attach to the previous word without extra spacing
bool isAttachingPunctuationWord(const std::string& word) {
  if (word.empty()) return false;
  size_t pos = 0;
  while (pos < word.size()) {
    bool matched = false;
    for (const auto& p : punctuation) {
      if (word.compare(pos, p.size(), p) == 0) {
        pos += p.size();
        matched = true;
        break;
      }
    }
    if (!matched) return false;
  }
  return true;
}

namespace {

// Find all soft hyphen byte positions in a UTF-8 string
std::vector<size_t> findSoftHyphenPositions(const std::string& word) {
  std::vector<size_t> positions;
  for (size_t i = 0; i + 1 < word.size(); ++i) {
    if (static_cast<unsigned char>(word[i]) == SOFT_HYPHEN_BYTE1 &&
        static_cast<unsigned char>(word[i + 1]) == SOFT_HYPHEN_BYTE2) {
      positions.push_back(i);
    }
  }
  return positions;
}

// Remove all soft hyphens from a string
std::string stripSoftHyphens(const std::string& word) {
  std::string result;
  result.reserve(word.size());
  size_t i = 0;
  while (i < word.size()) {
    if (i + 1 < word.size() && static_cast<unsigned char>(word[i]) == SOFT_HYPHEN_BYTE1 &&
        static_cast<unsigned char>(word[i + 1]) == SOFT_HYPHEN_BYTE2) {
      i += 2;  // Skip soft hyphen
    } else {
      result += word[i++];
    }
  }
  return result;
}

// Check if word ends with a soft hyphen marker (U+00AD = 0xC2 0xAD)
bool hasTrailingSoftHyphen(const std::string& word) {
  return word.size() >= 2 && static_cast<unsigned char>(word[word.size() - 2]) == SOFT_HYPHEN_BYTE1 &&
         static_cast<unsigned char>(word[word.size() - 1]) == SOFT_HYPHEN_BYTE2;
}

// Replace trailing soft hyphen with visible ASCII hyphen for rendering
std::string replaceTrailingSoftHyphen(std::string word) {
  if (hasTrailingSoftHyphen(word)) {
    word.resize(word.size() - 2);
    word += '-';
  }
  return word;
}

// Get word prefix before soft hyphen position (stripped) + visible hyphen
std::string getWordPrefix(const std::string& word, size_t softHyphenPos) {
  std::string prefix = word.substr(0, softHyphenPos);
  return stripSoftHyphens(prefix) + "-";
}

// Get word suffix after soft hyphen position (keep soft hyphens for further splitting)
std::string getWordSuffix(const std::string& word, size_t softHyphenPos) {
  return word.substr(softHyphenPos + 2);  // Skip past soft hyphen bytes, DON'T strip
}

// Check if codepoint is CJK ideograph (Unicode Line Break Class ID)
// Based on UAX #14 - allows line break before/after these characters
bool isCjkCodepoint(uint32_t cp) {
  // CJK Unified Ideographs
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
  // CJK Extension A
  if (cp >= 0x3400 && cp <= 0x4DBF) return true;
  // CJK Compatibility Ideographs
  if (cp >= 0xF900 && cp <= 0xFAFF) return true;
  // Hiragana
  if (cp >= 0x3040 && cp <= 0x309F) return true;
  // Katakana
  if (cp >= 0x30A0 && cp <= 0x30FF) return true;
  // Hangul Syllables
  if (cp >= 0xAC00 && cp <= 0xD7AF) return true;
  // CJK Extension B and beyond (Plane 2)
  if (cp >= 0x20000 && cp <= 0x2A6DF) return true;
  // Fullwidth ASCII variants (often used in CJK context)
  if (cp >= 0xFF00 && cp <= 0xFFEF) return true;
  return false;
}

// Knuth-Plass: Calculate badness (looseness) of a line
// Returns cubic ratio penalty - loose lines are penalized more heavily
float calculateBadness(int lineWidth, int targetWidth) {
  if (targetWidth <= 0) return INFINITY_PENALTY;
  if (lineWidth > targetWidth) return INFINITY_PENALTY;
  if (lineWidth == targetWidth) return 0.0f;
  float ratio = static_cast<float>(targetWidth - lineWidth) / static_cast<float>(targetWidth);
  return ratio * ratio * ratio * 100.0f;
}

// Knuth-Plass: Calculate demerits for a line based on its badness
// Last line gets 0 demerits (allowed to be loose)
float calculateDemerits(float badness, bool isLastLine) {
  if (badness >= INFINITY_PENALTY) return INFINITY_PENALTY;
  if (isLastLine) return 0.0f;
  return (1.0f + badness) * (1.0f + badness);
}

}  // namespace

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle) {
  if (word.empty()) return;

  // Check if word contains any CJK characters
  bool hasCjk = false;
  const unsigned char* check = reinterpret_cast<const unsigned char*>(word.c_str());
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&check))) {
    if (isCjkCodepoint(cp)) {
      hasCjk = true;
      break;
    }
  }

  if (!hasCjk) {
    // No CJK - keep as single word (Latin, accented Latin, Cyrillic, etc.)
    words.push_back(std::move(word));
    wordStyles.push_back(fontStyle);
    return;
  }

  // Mixed content: group non-CJK runs together, split CJK individually
  const unsigned char* p = reinterpret_cast<const unsigned char*>(word.c_str());
  std::string nonCjkBuf;

  while ((cp = utf8NextCodepoint(&p))) {
    if (isCjkCodepoint(cp)) {
      // CJK character - flush non-CJK buffer first, then add this char alone
      if (!nonCjkBuf.empty()) {
        words.push_back(std::move(nonCjkBuf));
        wordStyles.push_back(fontStyle);
        nonCjkBuf.clear();
      }

      // Re-encode CJK codepoint to UTF-8
      std::string buf;
      if (cp < 0x10000) {
        buf += static_cast<char>(0xE0 | (cp >> 12));
        buf += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf += static_cast<char>(0x80 | (cp & 0x3F));
      } else {
        buf += static_cast<char>(0xF0 | (cp >> 18));
        buf += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        buf += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf += static_cast<char>(0x80 | (cp & 0x3F));
      }
      words.push_back(buf);
      wordStyles.push_back(fontStyle);
    } else {
      // Non-CJK character - accumulate into buffer
      if (cp < 0x80) {
        nonCjkBuf += static_cast<char>(cp);
      } else if (cp < 0x800) {
        nonCjkBuf += static_cast<char>(0xC0 | (cp >> 6));
        nonCjkBuf += static_cast<char>(0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
        nonCjkBuf += static_cast<char>(0xE0 | (cp >> 12));
        nonCjkBuf += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        nonCjkBuf += static_cast<char>(0x80 | (cp & 0x3F));
      } else {
        nonCjkBuf += static_cast<char>(0xF0 | (cp >> 18));
        nonCjkBuf += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        nonCjkBuf += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        nonCjkBuf += static_cast<char>(0x80 | (cp & 0x3F));
      }
    }
  }

  // Flush any remaining non-CJK buffer
  if (!nonCjkBuf.empty()) {
    words.push_back(std::move(nonCjkBuf));
    wordStyles.push_back(fontStyle);
  }
}

// Consumes data to minimize memory usage
// Returns false if aborted, true otherwise
bool ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine, const AbortCallback& shouldAbort) {
  if (words.empty()) {
    return true;
  }

  // Check for abort before starting
  if (shouldAbort && shouldAbort()) {
    return false;
  }

  const int pageWidth = viewportWidth;
  const int spaceWidth = renderer.getSpaceWidth(fontId);

  // Rejoin words that were split by a previous interrupted greedy layout pass.
  // Split prefixes are marked with trailing U+00AD; rejoin with the following suffix word.
  {
    auto it = words.begin();
    auto sIt = wordStyles.begin();
    while (it != words.end()) {
      auto nextIt = std::next(it);
      if (nextIt != words.end() && hasTrailingSoftHyphen(*it)) {
        it->resize(it->size() - 2);  // Remove trailing U+00AD
        *it += *nextIt;              // Rejoin with suffix
        words.erase(nextIt);
        wordStyles.erase(std::next(sIt));
        // Don't advance - check if rejoined word also has marker (nested splits)
      } else {
        ++it;
        ++sIt;
      }
    }
  }

  // Pre-split oversized words at soft hyphen positions
  if (hyphenationEnabled) {
    if (!preSplitOversizedWords(renderer, fontId, pageWidth, shouldAbort)) {
      return false;  // Aborted
    }
  }

  auto wordWidths = calculateWordWidths(renderer, fontId);
  const auto lineBreakIndices =
      useGreedyBreaking ? computeLineBreaksGreedy(renderer, fontId, pageWidth, spaceWidth, wordWidths, shouldAbort)
                        : computeLineBreaks(pageWidth, spaceWidth, wordWidths, shouldAbort);

  // Check if we were aborted during line break computation
  if (shouldAbort && shouldAbort()) {
    return false;
  }

  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    if (shouldAbort && shouldAbort()) {
      return false;
    }
    extractLine(i, pageWidth, spaceWidth, wordWidths, lineBreakIndices, processLine);
  }
  return true;
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  const size_t totalWordCount = words.size();

  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(totalWordCount);

  // Add indentation at the beginning of first word in paragraph
  if (indentLevel > 0 && !words.empty() && style != TextBlock::CENTER_ALIGN) {
    std::string& first_word = words.front();
    switch (indentLevel) {
      case 2:  // Normal - em-space (U+2003)
        first_word.insert(0, "\xe2\x80\x83");
        break;
      case 3:  // Large - em-space + en-space (U+2003 + U+2002)
        first_word.insert(0, "\xe2\x80\x83\xe2\x80\x82");
        break;
      default:  // Fallback for unexpected values: single en-space (U+2002)
        first_word.insert(0, "\xe2\x80\x82");
        break;
    }
  }

  auto wordsIt = words.begin();
  auto wordStylesIt = wordStyles.begin();

  while (wordsIt != words.end()) {
    // Strip soft hyphens before measuring (they should be invisible)
    // After preSplitOversizedWords, words shouldn't contain soft hyphens,
    // but we strip here for safety and for when hyphenation is disabled
    std::string displayWord = stripSoftHyphens(*wordsIt);
    wordWidths.push_back(renderer.getTextWidth(fontId, displayWord.c_str(), *wordStylesIt));
    // Update the word in the list with the stripped version for rendering
    *wordsIt = std::move(displayWord);

    std::advance(wordsIt, 1);
    std::advance(wordStylesIt, 1);
  }

  return wordWidths;
}

std::vector<size_t> ParsedText::computeLineBreaks(const int pageWidth, const int spaceWidth,
                                                  const std::vector<uint16_t>& wordWidths,
                                                  const AbortCallback& shouldAbort) const {
  const size_t n = words.size();

  if (n == 0) {
    return {};
  }

  // Forward DP: minDemerits[i] = minimum demerits to reach position i (before word i)
  std::vector<float> minDemerits(n + 1, INFINITY_PENALTY);
  std::vector<int> prevBreak(n + 1, -1);
  minDemerits[0] = 0.0f;

  for (size_t i = 0; i < n; i++) {
    // Check for abort periodically (every 100 words in outer loop)
    if (shouldAbort && (i % 100 == 0) && shouldAbort()) {
      return {};  // Return empty to signal abort
    }

    if (minDemerits[i] >= INFINITY_PENALTY) continue;

    int lineWidth = -spaceWidth;  // First word won't have preceding space
    for (size_t j = i; j < n; j++) {
      lineWidth += wordWidths[j] + spaceWidth;

      if (lineWidth > pageWidth) {
        if (j == i) {
          // Oversized word: force onto its own line with high penalty
          float demerits = 100.0f + LINE_PENALTY;
          if (minDemerits[i] + demerits < minDemerits[j + 1]) {
            minDemerits[j + 1] = minDemerits[i] + demerits;
            prevBreak[j + 1] = static_cast<int>(i);
          }
        }
        break;
      }

      bool isLastLine = (j == n - 1);
      float badness = calculateBadness(lineWidth, pageWidth);
      float demerits = calculateDemerits(badness, isLastLine) + LINE_PENALTY;

      if (minDemerits[i] + demerits < minDemerits[j + 1]) {
        minDemerits[j + 1] = minDemerits[i] + demerits;
        prevBreak[j + 1] = static_cast<int>(i);
      }
    }
  }

  // Backtrack to reconstruct line break indices
  std::vector<size_t> lineBreakIndices;
  int pos = static_cast<int>(n);
  while (pos > 0 && prevBreak[pos] >= 0) {
    lineBreakIndices.push_back(static_cast<size_t>(pos));
    pos = prevBreak[pos];
  }
  std::reverse(lineBreakIndices.begin(), lineBreakIndices.end());

  // Fallback: if backtracking failed or chain is incomplete, use single-word-per-line
  // After the loop, pos should be 0 if we successfully traced back to the start.
  // If pos > 0, the chain is incomplete (no valid path from position 0 to n).
  if (lineBreakIndices.empty() || pos != 0) {
    lineBreakIndices.clear();
    for (size_t i = 1; i <= n; i++) {
      lineBreakIndices.push_back(i);
    }
  }

  return lineBreakIndices;
}

std::vector<size_t> ParsedText::computeLineBreaksGreedy(const GfxRenderer& renderer, const int fontId,
                                                        const int pageWidth, const int spaceWidth,
                                                        std::vector<uint16_t>& wordWidths,
                                                        const AbortCallback& shouldAbort) {
  std::vector<size_t> breaks;
  size_t n = wordWidths.size();

  if (n == 0) {
    return breaks;
  }

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  int lineWidth = -spaceWidth;  // First word won't have preceding space
  for (size_t i = 0; i < n; i++, ++wordIt, ++styleIt) {
    // Check for abort periodically (every 200 words)
    if (shouldAbort && (i % 200 == 0) && shouldAbort()) {
      return {};  // Return empty to signal abort
    }

    const int wordWidth = wordWidths[i];

    // Check if adding this word would overflow the line
    if (lineWidth + wordWidth + spaceWidth > pageWidth && lineWidth > 0) {
      // Try to hyphenate: split the overflowing word so its first part fits on this line
      const int remainingWidth = pageWidth - lineWidth - spaceWidth;
      if (remainingWidth > 0 &&
          trySplitWordForLineEnd(renderer, fontId, remainingWidth, wordIt, styleIt, i, wordWidths)) {
        // Word was split: prefix is at index i (fits on current line), suffix at i+1
        lineWidth += wordWidths[i] + spaceWidth;
        n = wordWidths.size();  // List grew by one
        // End this line after the prefix
        breaks.push_back(i + 1);
        // Next iteration (i+1) starts the suffix on a new line
        lineWidth = -spaceWidth;  // Will be updated when we process i+1
      } else {
        // No hyphenation possible - start a new line at this word
        breaks.push_back(i);
        lineWidth = wordWidth;
      }
    } else {
      lineWidth += wordWidth + spaceWidth;
    }
  }

  // Final break at end of all words
  breaks.push_back(n);
  return breaks;
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const int spaceWidth,
                             const std::vector<uint16_t>& wordWidths, const std::vector<size_t>& lineBreakIndices,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  // Calculate total word width for this line and count actual word gaps
  // (punctuation that attaches to previous word doesn't count as a gap)
  int lineWordWidthSum = 0;
  size_t actualGapCount = 0;
  auto countWordIt = words.begin();

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineWordWidthSum += wordWidths[lastBreakAt + wordIdx];
    // Count gaps: each word after the first creates a gap, unless it's attaching punctuation
    if (wordIdx > 0 && !isAttachingPunctuationWord(*countWordIt)) {
      actualGapCount++;
    }
    ++countWordIt;
  }

  // Calculate spacing
  const int spareSpace = pageWidth - lineWordWidthSum;

  int spacing = spaceWidth;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  // For justified text, calculate spacing based on actual gap count
  if (style == TextBlock::JUSTIFIED && !isLastLine && actualGapCount >= 1) {
    spacing = spareSpace / static_cast<int>(actualGapCount);
  }

  // For RTL text, default to right alignment
  const auto effectiveStyle = (isRtl && style == TextBlock::LEFT_ALIGN) ? TextBlock::RIGHT_ALIGN : style;

  // Build WordData vector directly, consuming from front of lists
  // Punctuation that attaches to the previous word doesn't get space before it
  std::vector<TextBlock::WordData> lineData;
  lineData.reserve(lineWordCount);

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();

  if (isRtl) {
    // RTL: Position words from right to left
    uint16_t xpos;
    if (effectiveStyle == TextBlock::CENTER_ALIGN) {
      xpos = pageWidth - (spareSpace - static_cast<int>(actualGapCount) * spacing) / 2;
    } else {
      xpos = pageWidth;  // RIGHT_ALIGN and JUSTIFIED start from right edge
    }

    for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
      const uint16_t currentWordWidth = wordWidths[lastBreakAt + wordIdx];
      xpos -= currentWordWidth;
      lineData.push_back({replaceTrailingSoftHyphen(std::move(*wordIt)), xpos, *styleIt});

      auto nextWordIt = wordIt;
      ++nextWordIt;
      const bool nextIsAttachingPunctuation = wordIdx + 1 < lineWordCount && isAttachingPunctuationWord(*nextWordIt);
      xpos -= (nextIsAttachingPunctuation ? 0 : spacing);
      ++wordIt;
      ++styleIt;
    }
  } else {
    // LTR: Position words from left to right
    uint16_t xpos = 0;
    if (effectiveStyle == TextBlock::RIGHT_ALIGN) {
      xpos = spareSpace - static_cast<int>(actualGapCount) * spaceWidth;
    } else if (effectiveStyle == TextBlock::CENTER_ALIGN) {
      xpos = (spareSpace - static_cast<int>(actualGapCount) * spaceWidth) / 2;
    }

    for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
      const uint16_t currentWordWidth = wordWidths[lastBreakAt + wordIdx];
      lineData.push_back({replaceTrailingSoftHyphen(std::move(*wordIt)), xpos, *styleIt});

      auto nextWordIt = wordIt;
      ++nextWordIt;
      const bool nextIsAttachingPunctuation = wordIdx + 1 < lineWordCount && isAttachingPunctuationWord(*nextWordIt);
      xpos += currentWordWidth + (nextIsAttachingPunctuation ? 0 : spacing);
      ++wordIt;
      ++styleIt;
    }
  }

  // Remove consumed elements from lists
  words.erase(words.begin(), wordIt);
  wordStyles.erase(wordStyles.begin(), styleIt);

  processLine(std::make_shared<TextBlock>(std::move(lineData), effectiveStyle));
}

bool ParsedText::preSplitOversizedWords(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                        const AbortCallback& shouldAbort) {
  std::list<std::string> newWords;
  std::list<EpdFontFamily::Style> newStyles;

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  size_t wordCount = 0;

  while (wordIt != words.end()) {
    // Check for abort periodically (every 50 words)
    if (shouldAbort && (++wordCount % 50 == 0) && shouldAbort()) {
      return false;  // Aborted
    }

    const std::string& word = *wordIt;
    const EpdFontFamily::Style wordStyle = *styleIt;

    // Measure word without soft hyphens
    const std::string stripped = stripSoftHyphens(word);
    const int wordWidth = renderer.getTextWidth(fontId, stripped.c_str(), wordStyle);

    if (wordWidth <= pageWidth) {
      // Word fits, keep as-is (will be stripped later in calculateWordWidths)
      newWords.push_back(word);
      newStyles.push_back(wordStyle);
    } else {
      // Word is too wide - try to split at soft hyphen positions
      auto shyPositions = findSoftHyphenPositions(word);

      if (shyPositions.empty()) {
        // No soft hyphens - use dictionary-based hyphenation
        // Compute all break points on the full word once (Liang patterns
        // need full-word context for correct results).
        auto breaks = Hyphenation::breakOffsets(word, true);

        if (breaks.empty()) {
          newWords.push_back(word);
          newStyles.push_back(wordStyle);
        } else {
          size_t prevOffset = 0;

          for (size_t bi = 0; bi <= breaks.size(); ++bi) {
            const std::string remaining = word.substr(prevOffset);
            const int remainingWidth = renderer.getTextWidth(fontId, remaining.c_str(), wordStyle);

            if (remainingWidth <= pageWidth) {
              newWords.push_back(remaining);
              newStyles.push_back(wordStyle);
              break;
            }

            // Find the rightmost break where prefix + hyphen fits
            int bestIdx = -1;
            std::string bestPrefix;
            for (int i = static_cast<int>(breaks.size()) - 1; i >= 0; --i) {
              if (breaks[i].byteOffset <= prevOffset) continue;
              std::string prefix = word.substr(prevOffset, breaks[i].byteOffset - prevOffset);
              if (breaks[i].requiresInsertedHyphen) {
                prefix += "-";
              }
              const int prefixWidth = renderer.getTextWidth(fontId, prefix.c_str(), wordStyle);
              if (prefixWidth <= pageWidth) {
                bestIdx = i;
                bestPrefix = std::move(prefix);
                break;
              }
            }

            if (bestIdx < 0) {
              newWords.push_back(remaining);
              newStyles.push_back(wordStyle);
              break;
            }

            newWords.push_back(std::move(bestPrefix));
            newStyles.push_back(wordStyle);
            prevOffset = breaks[bestIdx].byteOffset;
          }
        }
      } else {
        // Split word at soft hyphen positions
        std::string remaining = word;
        size_t splitIterations = 0;
        constexpr size_t MAX_SPLIT_ITERATIONS = 100;  // Safety limit

        while (splitIterations++ < MAX_SPLIT_ITERATIONS) {
          if (splitIterations == MAX_SPLIT_ITERATIONS) {
            LOG_ERR(TAG, "Warning: hit max split iterations for oversized word");
          }
          const std::string strippedRemaining = stripSoftHyphens(remaining);
          const int remainingWidth = renderer.getTextWidth(fontId, strippedRemaining.c_str(), wordStyle);

          if (remainingWidth <= pageWidth) {
            // Remaining part fits, add it and done
            newWords.push_back(remaining);
            newStyles.push_back(wordStyle);
            break;
          }

          // Find soft hyphen positions in remaining string
          auto localPositions = findSoftHyphenPositions(remaining);
          if (localPositions.empty()) {
            // No more soft hyphens, output as-is
            newWords.push_back(remaining);
            newStyles.push_back(wordStyle);
            break;
          }

          // Find the rightmost soft hyphen where prefix + hyphen fits
          int bestPos = -1;
          for (int i = static_cast<int>(localPositions.size()) - 1; i >= 0; --i) {
            std::string prefix = getWordPrefix(remaining, localPositions[i]);
            int prefixWidth = renderer.getTextWidth(fontId, prefix.c_str(), wordStyle);
            if (prefixWidth <= pageWidth) {
              bestPos = i;
              break;
            }
          }

          if (bestPos < 0) {
            // Even the smallest prefix is too wide - output as-is
            newWords.push_back(remaining);
            newStyles.push_back(wordStyle);
            break;
          }

          // Split at this position
          std::string prefix = getWordPrefix(remaining, localPositions[bestPos]);
          std::string suffix = getWordSuffix(remaining, localPositions[bestPos]);

          newWords.push_back(prefix);  // Already includes visible hyphen "-"
          newStyles.push_back(wordStyle);

          if (suffix.empty()) {
            break;
          }
          remaining = suffix;
        }
      }
    }

    ++wordIt;
    ++styleIt;
  }

  words = std::move(newWords);
  wordStyles = std::move(newStyles);
  return true;
}

bool ParsedText::trySplitWordForLineEnd(const GfxRenderer& renderer, const int fontId, const int remainingWidth,
                                        std::list<std::string>::iterator wordIt,
                                        std::list<EpdFontFamily::Style>::iterator styleIt, const size_t wordIndex,
                                        std::vector<uint16_t>& wordWidths) {
  if (!hyphenationEnabled) return false;

  const std::string& word = *wordIt;
  const EpdFontFamily::Style fontStyle = *styleIt;

  auto breaks = Hyphenation::breakOffsets(word, false);
  if (breaks.empty()) return false;

  // Find rightmost break where prefix+hyphen fits in remainingWidth
  for (int i = static_cast<int>(breaks.size()) - 1; i >= 0; --i) {
    std::string prefix = word.substr(0, breaks[i].byteOffset);
    // Measure with visible hyphen for accurate layout
    const std::string displayPrefix = breaks[i].requiresInsertedHyphen ? prefix + "-" : prefix;
    const int prefixWidth = renderer.getTextWidth(fontId, displayPrefix.c_str(), fontStyle);
    if (prefixWidth <= remainingWidth) {
      // Store with soft hyphen MARKER (not visible hyphen) so interrupted layouts
      // can rejoin the fragments on resume (calculateWordWidths strips U+00AD)
      if (breaks[i].requiresInsertedHyphen) prefix += "\xC2\xAD";
      std::string suffix = word.substr(breaks[i].byteOffset);
      const int suffixWidth = renderer.getTextWidth(fontId, suffix.c_str(), fontStyle);

      // Replace current word with prefix, insert suffix after
      *wordIt = std::move(prefix);
      auto nextWordIt = std::next(wordIt);
      auto nextStyleIt = std::next(styleIt);
      words.insert(nextWordIt, std::move(suffix));
      wordStyles.insert(nextStyleIt, fontStyle);

      // Update widths vector
      wordWidths[wordIndex] = static_cast<uint16_t>(prefixWidth);
      wordWidths.insert(wordWidths.begin() + wordIndex + 1, static_cast<uint16_t>(suffixWidth));
      return true;
    }
  }
  return false;
}
