/**
 * MarkdownParser.h
 *
 * Markdown parser using md_parser for tokenization.
 * Implements ContentParser interface for integration with PageCache.
 * Reads directly from SD card with minimal memory usage.
 */

#pragma once

#include <ContentParser.h>
#include <Epub/RenderConfig.h>
#include <SdFat.h>

#include <functional>
#include <memory>
#include <string>

#include "md_parser.h"

class Page;
class GfxRenderer;
class ParsedText;
class TextBlock;

constexpr int MAX_WORD_SIZE = 200;
constexpr int LINE_BUFFER_SIZE = 512;

/**
 * Content parser for Markdown files using md_parser tokenization.
 * Parses markdown syntax (headers, bold, italic, lists, etc.) into styled text.
 * Minimal memory usage - reads line by line from SD card.
 */
class MarkdownParser : public ContentParser {
 public:
  MarkdownParser(std::string filepath, GfxRenderer& renderer, const RenderConfig& config);
  ~MarkdownParser() override;

  bool parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages = 0) override;
  bool hasMoreContent() const override { return hasMore_; }
  void reset() override;

 private:
  // File state
  std::string filepath_;
  GfxRenderer& renderer_;
  RenderConfig config_;
  size_t fileSize_ = 0;
  size_t currentOffset_ = 0;
  bool hasMore_ = true;

  // Line buffer for reading from file
  char lineBuffer_[LINE_BUFFER_SIZE];

  // Parsing context passed through md_parser callback
  struct ParseContext {
    MarkdownParser* self;
    std::unique_ptr<ParsedText> textBlock;
    std::unique_ptr<Page> currentPage;
    int16_t pageNextY;
    bool inBold;
    bool inItalic;
    bool inCodeBlock;
    int headerLevel;
    bool hitMaxPages;
    uint16_t pagesCreated;
    uint16_t maxPages;
    std::function<void(std::unique_ptr<Page>)> onPageComplete;

    // Word buffer for building words
    char wordBuffer[MAX_WORD_SIZE + 1];
    int wordBufferIndex;
  };

  // Static callback for md_parser
  static bool tokenCallback(const md_token_t* token, void* userData);

  // Helpers
  bool readLine(FsFile& file);
  void flushWordBuffer(ParseContext& ctx);
  void flushTextBlock(ParseContext& ctx);
  bool addLineToPage(ParseContext& ctx, std::shared_ptr<TextBlock> line);
  int getCurrentFontStyle(const ParseContext& ctx) const;
  void startNewTextBlock(ParseContext& ctx, int style);
};
