#pragma once

#include <ContentParser.h>
#include <EpdFontFamily.h>
#include <Epub/RenderConfig.h>
#include <Epub/blocks/TextBlock.h>
#include <ScriptDetector.h>
#include <expat.h>

#include <climits>
#include <functional>
#include <memory>
#include <string>

class Page;
class GfxRenderer;
class ParsedText;

class Fb2Parser : public ContentParser {
 public:
  Fb2Parser(std::string filepath, GfxRenderer& renderer, const RenderConfig& config);
  ~Fb2Parser() override;

  bool parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages = 0,
                  const AbortCallback& shouldAbort = nullptr) override;
  bool hasMoreContent() const override { return hasMore_; }
  bool canResume() const override { return false; }
  void reset() override;
  const std::vector<std::pair<std::string, uint16_t>>& getAnchorMap() const override { return anchorMap_; }

 private:
  std::string filepath_;
  GfxRenderer& renderer_;
  RenderConfig config_;
  bool hasMore_ = true;
  bool isRtl_ = false;

  // Expat state
  XML_Parser xmlParser_ = nullptr;
  bool stopRequested_ = false;

  // XML depth tracking
  int depth_ = 0;
  int skipUntilDepth_ = INT_MAX;
  int boldUntilDepth_ = INT_MAX;
  int italicUntilDepth_ = INT_MAX;
  bool inBody_ = false;
  bool inTitle_ = false;
  bool inSubtitle_ = false;
  bool inParagraph_ = false;
  int bodyCount_ = 0;
  int sectionCounter_ = 0;
  bool firstSection_ = true;

  // Word buffer
  static constexpr int MAX_WORD_SIZE = 200;
  char partWordBuffer_[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex_ = 0;

  // Page building
  std::unique_ptr<ParsedText> currentTextBlock_;
  std::unique_ptr<Page> currentPage_;
  int16_t currentPageNextY_ = 0;

  // Anchor map for TOC navigation (section_N → page index)
  std::vector<std::pair<std::string, uint16_t>> anchorMap_;

  // Callback state
  std::function<void(std::unique_ptr<Page>)> onPageComplete_;
  uint16_t maxPages_ = 0;
  uint16_t pagesCreated_ = 0;
  bool hitMaxPages_ = false;
  AbortCallback shouldAbort_;

  // File reading
  size_t fileSize_ = 0;

  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);

  void flushPartWordBuffer();
  void startNewTextBlock(TextBlock::BLOCK_STYLE style);
  void makePages();
  void addLineToPage(std::shared_ptr<TextBlock> line);
  void startNewPage();
  EpdFontFamily::Style getCurrentFontFamily() const;
  void addVerticalSpacing(int lines);
};
