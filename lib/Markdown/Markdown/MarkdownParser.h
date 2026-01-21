/**
 * MarkdownParser.h
 *
 * Markdown parser for Papyrix Reader using MD4C
 * Parses markdown content and builds pages for rendering
 */

#pragma once

#include <climits>
#include <functional>
#include <memory>
#include <string>

#include "../Markdown.h"
#include "Epub/ParsedText.h"
#include "Epub/RenderConfig.h"
#include "Epub/blocks/TextBlock.h"

class Page;
class GfxRenderer;

#define MAX_WORD_SIZE 200

class MarkdownParser {
  const std::shared_ptr<Markdown>& markdown;
  GfxRenderer& renderer;
  std::function<void(std::unique_ptr<Page>)> completePageFn;
  std::function<void(int)> progressFn;

  // Parsing state
  int boldDepth = 0;
  int italicDepth = 0;
  int headerLevel = 0;
  bool inListItem = false;
  bool firstListItemWord = false;

  // Word buffer
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;

  // Current text block and page being built
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  RenderConfig config;

  // MD4C callbacks (use MD4C types directly to match callback signatures)
  static int enterBlockCallback(int blockType, void* detail, void* userdata);
  static int leaveBlockCallback(int blockType, void* detail, void* userdata);
  static int enterSpanCallback(int spanType, void* detail, void* userdata);
  static int leaveSpanCallback(int spanType, void* detail, void* userdata);
  static int textCallback(int textType, const char* text, unsigned size, void* userdata);

  // Page building
  void startNewTextBlock(TextBlock::BLOCK_STYLE style);
  void makePages();
  void addLineToPage(std::shared_ptr<TextBlock> line);
  void flushPartWordBuffer();
  EpdFontFamily::Style getCurrentFontStyle() const;

 public:
  explicit MarkdownParser(const std::shared_ptr<Markdown>& markdown, GfxRenderer& renderer, const RenderConfig& config,
                          const std::function<void(std::unique_ptr<Page>)>& completePageFn,
                          const std::function<void(int)>& progressFn = nullptr)
      : markdown(markdown),
        renderer(renderer),
        config(config),
        completePageFn(completePageFn),
        progressFn(progressFn) {}
  ~MarkdownParser() = default;

  bool parseAndBuildPages();
};
