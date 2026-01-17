#include "ParsedText.h"

#include <GfxRenderer.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

constexpr int MAX_COST = std::numeric_limits<int>::max();

// Soft hyphen (U+00AD) as UTF-8 bytes
constexpr unsigned char SOFT_HYPHEN_BYTE1 = 0xC2;
constexpr unsigned char SOFT_HYPHEN_BYTE2 = 0xAD;

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
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  const int pageWidth = viewportWidth;
  const int spaceWidth = renderer.getSpaceWidth(fontId);

  // Pre-split oversized words at soft hyphen positions
  if (hyphenationEnabled) {
    preSplitOversizedWords(renderer, fontId, pageWidth);
  }

  const auto wordWidths = calculateWordWidths(renderer, fontId);
  const auto lineBreakIndices = computeLineBreaks(pageWidth, spaceWidth, wordWidths);
  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, spaceWidth, wordWidths, lineBreakIndices, processLine);
  }
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  const size_t totalWordCount = words.size();

  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(totalWordCount);

  // add em-space at the beginning of first word in paragraph to indent
  if (!extraParagraphSpacing) {
    std::string& first_word = words.front();
    first_word.insert(0, "\xe2\x80\x83");
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
                                                  const std::vector<uint16_t>& wordWidths) const {
  const size_t totalWordCount = words.size();

  // DP table to store the minimum badness (cost) of lines starting at index i
  std::vector<int> dp(totalWordCount);
  // 'ans[i]' stores the index 'j' of the *last word* in the optimal line starting at 'i'
  // Using uint16_t saves memory (was size_t/4 bytes on ESP32, now 2 bytes per word)
  std::vector<uint16_t> ans(totalWordCount);

  // Base Case
  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int currlen = -spaceWidth;
    dp[i] = MAX_COST;

    for (size_t j = i; j < totalWordCount; ++j) {
      // Current line length: previous width + space + current word width
      currlen += wordWidths[j] + spaceWidth;

      if (currlen > pageWidth) {
        break;
      }

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0;  // Last line
      } else {
        const int remainingSpace = pageWidth - currlen;
        // Use long long for the square to prevent overflow
        const long long cost_ll = static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];

        if (cost_ll > MAX_COST) {
          cost = MAX_COST;
        } else {
          cost = static_cast<int>(cost_ll);
        }
      }

      if (cost < dp[i]) {
        dp[i] = cost;
        ans[i] = j;  // j is the index of the last word in this optimal line
      }
    }

    // Handle oversized word: if no valid configuration found, force single-word line
    // This prevents cascade failure where one oversized word breaks all preceding words
    if (dp[i] == MAX_COST) {
      ans[i] = i;  // Just this word on its own line
      // Inherit cost from next word to allow subsequent words to find valid configurations
      if (i + 1 < static_cast<int>(totalWordCount)) {
        dp[i] = dp[i + 1];
      } else {
        dp[i] = 0;
      }
    }
  }

  // Stores the index of the word that starts the next line (last_word_index + 1)
  std::vector<size_t> lineBreakIndices;
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;

    // Safety check: prevent infinite loop if nextBreakIndex doesn't advance
    if (nextBreakIndex <= currentWordIndex) {
      // Force advance by at least one word to avoid infinite loop
      nextBreakIndex = currentWordIndex + 1;
    }

    lineBreakIndices.push_back(nextBreakIndex);
    currentWordIndex = nextBreakIndex;
  }

  return lineBreakIndices;
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const int spaceWidth,
                             const std::vector<uint16_t>& wordWidths, const std::vector<size_t>& lineBreakIndices,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  // Calculate total word width for this line
  int lineWordWidthSum = 0;
  for (size_t i = lastBreakAt; i < lineBreak; i++) {
    lineWordWidthSum += wordWidths[i];
  }

  // Calculate spacing
  const int spareSpace = pageWidth - lineWordWidthSum;

  int spacing = spaceWidth;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  if (style == TextBlock::JUSTIFIED && !isLastLine && lineWordCount >= 2) {
    spacing = spareSpace / (lineWordCount - 1);
  }

  // Calculate initial x position
  uint16_t xpos = 0;
  if (style == TextBlock::RIGHT_ALIGN) {
    xpos = spareSpace - (lineWordCount - 1) * spaceWidth;
  } else if (style == TextBlock::CENTER_ALIGN) {
    xpos = (spareSpace - (lineWordCount - 1) * spaceWidth) / 2;
  }

  // Build WordData vector directly, consuming from front of lists
  std::vector<TextBlock::WordData> lineData;
  lineData.reserve(lineWordCount);

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  for (size_t i = lastBreakAt; i < lineBreak; i++) {
    const uint16_t currentWordWidth = wordWidths[i];
    lineData.push_back({std::move(*wordIt), xpos, *styleIt});
    xpos += currentWordWidth + spacing;
    ++wordIt;
    ++styleIt;
  }

  // Remove consumed elements from lists
  words.erase(words.begin(), wordIt);
  wordStyles.erase(wordStyles.begin(), styleIt);

  processLine(std::make_shared<TextBlock>(std::move(lineData), style));
}

void ParsedText::preSplitOversizedWords(const GfxRenderer& renderer, const int fontId, const int pageWidth) {
  std::list<std::string> newWords;
  std::list<EpdFontFamily::Style> newStyles;

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();

  while (wordIt != words.end()) {
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
        // No soft hyphens - use GfxRenderer's hard hyphenation helper
        auto chunks = renderer.breakWordWithHyphenation(fontId, word.c_str(), pageWidth, wordStyle);
        for (const auto& chunk : chunks) {
          newWords.push_back(chunk);
          newStyles.push_back(wordStyle);
        }
      } else {
        // Split word at soft hyphen positions
        std::string remaining = word;

        while (true) {
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
}
