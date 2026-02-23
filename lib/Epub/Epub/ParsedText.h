#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "blocks/TextBlock.h"

class GfxRenderer;

/**
 * Callback type for checking if operation should abort.
 * Returns true if caller should stop work and return early.
 */
using AbortCallback = std::function<bool()>;

class ParsedText {
  std::list<std::string> words;
  std::list<EpdFontFamily::Style> wordStyles;
  TextBlock::BLOCK_STYLE style;
  uint8_t indentLevel;
  bool hyphenationEnabled;
  bool useGreedyBreaking = true;  // Default to greedy to avoid Knuth-Plass memory spike
  bool isRtl = false;

  std::vector<size_t> computeLineBreaks(int pageWidth, int spaceWidth, const std::vector<uint16_t>& wordWidths,
                                        const AbortCallback& shouldAbort = nullptr) const;
  std::vector<size_t> computeLineBreaksGreedy(const GfxRenderer& renderer, int fontId, int pageWidth, int spaceWidth,
                                              std::vector<uint16_t>& wordWidths,
                                              const AbortCallback& shouldAbort = nullptr);
  bool trySplitWordForLineEnd(const GfxRenderer& renderer, int fontId, int remainingWidth,
                              std::list<std::string>::iterator wordIt,
                              std::list<EpdFontFamily::Style>::iterator styleIt, size_t wordIndex,
                              std::vector<uint16_t>& wordWidths);
  void extractLine(size_t breakIndex, int pageWidth, int spaceWidth, const std::vector<uint16_t>& wordWidths,
                   const std::vector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine);
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId);
  bool preSplitOversizedWords(const GfxRenderer& renderer, int fontId, int pageWidth,
                              const AbortCallback& shouldAbort = nullptr);

 public:
  explicit ParsedText(const TextBlock::BLOCK_STYLE style, const uint8_t indentLevel,
                      const bool hyphenationEnabled = true, const bool useGreedy = true, const bool rtl = false)
      : style(style),
        indentLevel(indentLevel),
        hyphenationEnabled(hyphenationEnabled),
        useGreedyBreaking(useGreedy),
        isRtl(rtl) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle);
  void setStyle(const TextBlock::BLOCK_STYLE style) { this->style = style; }
  void setRtl(const bool rtl) { isRtl = rtl; }
  void setUseGreedyBreaking(const bool greedy) { useGreedyBreaking = greedy; }
  TextBlock::BLOCK_STYLE getStyle() const { return style; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  bool layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true, const AbortCallback& shouldAbort = nullptr);
};
