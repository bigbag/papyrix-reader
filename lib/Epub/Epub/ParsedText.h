#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "blocks/TextBlock.h"

class GfxRenderer;

class ParsedText {
  std::list<std::string> words;
  std::list<EpdFontFamily::Style> wordStyles;
  TextBlock::BLOCK_STYLE style;
  uint8_t indentLevel;
  bool hyphenationEnabled;
  bool useGreedyBreaking = true;  // Default to greedy to avoid Knuth-Plass memory spike

  std::vector<size_t> computeLineBreaks(int pageWidth, int spaceWidth, const std::vector<uint16_t>& wordWidths) const;
  std::vector<size_t> computeLineBreaksGreedy(int pageWidth, int spaceWidth,
                                              const std::vector<uint16_t>& wordWidths) const;
  void extractLine(size_t breakIndex, int pageWidth, int spaceWidth, const std::vector<uint16_t>& wordWidths,
                   const std::vector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine);
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId);
  void preSplitOversizedWords(const GfxRenderer& renderer, int fontId, int pageWidth);

 public:
  explicit ParsedText(const TextBlock::BLOCK_STYLE style, const uint8_t indentLevel,
                      const bool hyphenationEnabled = true, const bool useGreedy = false)
      : style(style), indentLevel(indentLevel), hyphenationEnabled(hyphenationEnabled), useGreedyBreaking(useGreedy) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle);
  void setStyle(const TextBlock::BLOCK_STYLE style) { this->style = style; }
  void setUseGreedyBreaking(const bool greedy) { useGreedyBreaking = greedy; }
  TextBlock::BLOCK_STYLE getStyle() const { return style; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true);
};
