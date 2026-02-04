#pragma once

#include <expat.h>

#include <climits>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

#include "../ParsedText.h"
#include "../RenderConfig.h"
#include "../blocks/ImageBlock.h"
#include "../blocks/TextBlock.h"
#include "../css/CssParser.h"
#include "DataUriStripper.h"

class Page;
class GfxRenderer;
class Print;

#define MAX_WORD_SIZE 200
constexpr int MAX_XML_DEPTH = 100;

class ChapterHtmlSlimParser {
  const std::string& filepath;
  GfxRenderer& renderer;
  std::function<bool(std::unique_ptr<Page>)> completePageFn;  // Returns false to stop parsing
  std::function<void(int)> progressFn;                        // Progress callback (0-100)
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  int cssBoldUntilDepth = INT_MAX;
  int cssItalicUntilDepth = INT_MAX;
  // buffer for building up words from characters, will auto break if longer than this
  // leave one char at end for null pointer
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  RenderConfig config;

  // Image support
  std::string chapterBasePath;
  std::string imageCachePath;
  std::function<bool(const std::string&, Print&, size_t)> readItemFn;

  // CSS support
  const CssParser* cssParser_ = nullptr;

  // XML parser handle for stopping mid-parse
  XML_Parser xmlParser_ = nullptr;
  bool stopRequested_ = false;

  // External abort callback for cooperative cancellation
  std::function<bool()> externalAbortCallback_ = nullptr;

  // Image failure rate limiting - skip remaining images after consecutive failures
  uint8_t consecutiveImageFailures_ = 0;
  static constexpr uint8_t MAX_CONSECUTIVE_IMAGE_FAILURES = 3;

  // Parser safety - timeout and memory checks
  uint32_t parseStartTime_ = 0;
  uint16_t loopCounter_ = 0;
  uint16_t pagesCreated_ = 0;
  static constexpr uint32_t MAX_PARSE_TIME_MS = 20000;   // 20 second timeout
  static constexpr uint16_t YIELD_CHECK_INTERVAL = 100;  // Check every 100 iterations
  static constexpr size_t MIN_FREE_HEAP = 8192;          // 8KB minimum free heap

  // Pre-parse data URI stripper to prevent expat OOM on large embedded images
  DataUriStripper dataUriStripper_;

  // Check if parsing should abort due to timeout or memory pressure
  bool shouldAbort() const;

  void startNewTextBlock(TextBlock::BLOCK_STYLE style);
  void flushPartWordBuffer();
  void makePages();
  std::string cacheImage(const std::string& src);
  void addImageToPage(std::shared_ptr<ImageBlock> image);
  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  explicit ChapterHtmlSlimParser(const std::string& filepath, GfxRenderer& renderer, const RenderConfig& config,
                                 const std::function<bool(std::unique_ptr<Page>)>& completePageFn,
                                 const std::function<void(int)>& progressFn = nullptr,
                                 const std::string& chapterBasePath = "", const std::string& imageCachePath = "",
                                 const std::function<bool(const std::string&, Print&, size_t)>& readItemFn = nullptr,
                                 const CssParser* cssParser = nullptr,
                                 const std::function<bool()>& externalAbortCallback = nullptr)
      : filepath(filepath),
        renderer(renderer),
        config(config),
        completePageFn(completePageFn),
        progressFn(progressFn),
        chapterBasePath(chapterBasePath),
        imageCachePath(imageCachePath),
        readItemFn(readItemFn),
        cssParser_(cssParser),
        externalAbortCallback_(externalAbortCallback) {}
  ~ChapterHtmlSlimParser() = default;
  bool parseAndBuildPages();
  void addLineToPage(std::shared_ptr<TextBlock> line);
};
