#include "Fb2Parser.h"

#include <Epub/Page.h>
#include <Epub/ParsedText.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Utf8.h>

#include <cstring>
#include <utility>

namespace {
constexpr size_t READ_CHUNK_SIZE = 4096;

bool isWhitespace(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

const char* stripNamespace(const char* name) {
  const char* local = strrchr(name, ':');
  return local ? local + 1 : name;
}
}  // namespace

Fb2Parser::Fb2Parser(std::string filepath, GfxRenderer& renderer, const RenderConfig& config)
    : filepath_(std::move(filepath)), renderer_(renderer), config_(config) {}

Fb2Parser::~Fb2Parser() {
  if (xmlParser_) {
    XML_ParserFree(xmlParser_);
    xmlParser_ = nullptr;
  }
}

void Fb2Parser::reset() {
  if (xmlParser_) {
    XML_ParserFree(xmlParser_);
    xmlParser_ = nullptr;
  }
  hasMore_ = true;
  isRtl_ = false;
  stopRequested_ = false;
  depth_ = 0;
  skipUntilDepth_ = INT_MAX;
  boldUntilDepth_ = INT_MAX;
  italicUntilDepth_ = INT_MAX;
  inBody_ = false;
  inTitle_ = false;
  inSubtitle_ = false;
  inParagraph_ = false;
  bodyCount_ = 0;
  sectionCounter_ = 0;
  firstSection_ = true;
  partWordBufferIndex_ = 0;
  currentTextBlock_.reset();
  currentPage_.reset();
  currentPageNextY_ = 0;
  pagesCreated_ = 0;
  hitMaxPages_ = false;
  fileSize_ = 0;
  anchorMap_.clear();
}

bool Fb2Parser::parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages,
                           const AbortCallback& shouldAbort) {
  FsFile file;
  if (!SdMan.openFileForRead("FB2", filepath_, file)) {
    Serial.printf("[FB2] Failed to open file: %s\n", filepath_.c_str());
    return false;
  }

  fileSize_ = file.size();
  onPageComplete_ = onPageComplete;
  maxPages_ = maxPages;
  pagesCreated_ = 0;
  hitMaxPages_ = false;
  stopRequested_ = false;
  shouldAbort_ = shouldAbort;

  // Single buffer reused for RTL peek and parsing (saves 4KB stack)
  uint8_t buffer[READ_CHUNK_SIZE + 1];

  // RTL detection on first chunk
  size_t peekBytes = file.read(buffer, READ_CHUNK_SIZE);
  if (peekBytes > 0) {
    buffer[peekBytes] = '\0';
    isRtl_ = ScriptDetector::containsArabic(reinterpret_cast<const char*>(buffer));
  }
  file.seekSet(0);

  // Create Expat parser
  xmlParser_ = XML_ParserCreate("UTF-8");
  if (!xmlParser_) {
    Serial.printf("[FB2] Failed to create XML parser\n");
    file.close();
    return false;
  }

  XML_SetUserData(xmlParser_, this);
  XML_SetElementHandler(xmlParser_, startElement, endElement);
  XML_SetCharacterDataHandler(xmlParser_, characterData);

  startNewPage();
  uint16_t abortCheckCounter = 0;

  while (file.available() > 0) {
    if (shouldAbort_ && (++abortCheckCounter % 10 == 0) && shouldAbort_()) {
      Serial.printf("[FB2] Aborted by external request\n");
      XML_ParserFree(xmlParser_);
      xmlParser_ = nullptr;
      file.close();
      hasMore_ = true;
      return false;
    }

    size_t bytesRead = file.read(buffer, READ_CHUNK_SIZE);
    if (bytesRead == 0) break;

    int done = (file.available() == 0) ? 1 : 0;
    if (XML_Parse(xmlParser_, reinterpret_cast<const char*>(buffer), static_cast<int>(bytesRead), done) ==
        XML_STATUS_ERROR) {
      Serial.printf("[FB2] Parse error at line %lu: %s\n", XML_GetCurrentLineNumber(xmlParser_),
                    XML_ErrorString(XML_GetErrorCode(xmlParser_)));
      XML_ParserFree(xmlParser_);
      xmlParser_ = nullptr;
      file.close();
      return false;
    }

    if (stopRequested_) {
      XML_ParserFree(xmlParser_);
      xmlParser_ = nullptr;
      file.close();
      hasMore_ = true;
      return true;
    }
  }

  // Flush remaining content
  flushPartWordBuffer();
  if (currentTextBlock_ && !currentTextBlock_->isEmpty()) {
    makePages();
  }

  // Emit final page
  if (currentPage_ && !currentPage_->elements.empty()) {
    onPageComplete_(std::move(currentPage_));
    pagesCreated_++;
  }

  XML_ParserFree(xmlParser_);
  xmlParser_ = nullptr;
  file.close();
  currentTextBlock_.reset();
  currentPage_.reset();
  hasMore_ = false;

  Serial.printf("[FB2] Parsed %d pages from %s\n", pagesCreated_, filepath_.c_str());
  return true;
}

void XMLCALL Fb2Parser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<Fb2Parser*>(userData);
  const char* localName = stripNamespace(name);

  // Prevent stack overflow from deeply nested XML
  if (self->depth_ >= 100) {
    self->depth_++;
    return;
  }

  if (self->skipUntilDepth_ < self->depth_) {
    self->depth_++;
    return;
  }

  if (strcmp(localName, "binary") == 0) {
    self->skipUntilDepth_ = self->depth_;
    self->depth_++;
    return;
  }

  if (strcmp(localName, "body") == 0) {
    self->bodyCount_++;
    self->inBody_ = (self->bodyCount_ == 1);
    self->depth_++;
    return;
  }

  if (!self->inBody_) {
    self->depth_++;
    return;
  }

  if (strcmp(localName, "section") == 0) {
    self->sectionCounter_++;
    if (!self->firstSection_) {
      // Flush current content before new section
      self->flushPartWordBuffer();
      if (self->currentTextBlock_ && !self->currentTextBlock_->isEmpty()) {
        self->makePages();
      }
      // Section break: start new page
      if (self->currentPage_ && !self->currentPage_->elements.empty()) {
        self->onPageComplete_(std::move(self->currentPage_));
        self->pagesCreated_++;
        if (self->maxPages_ > 0 && self->pagesCreated_ >= self->maxPages_) {
          self->hitMaxPages_ = true;
          self->stopRequested_ = true;
          self->depth_++;
          return;
        }
      }
      self->startNewPage();
    }
    self->firstSection_ = false;
    // Record anchor for TOC navigation: section_N → page where this section starts
    self->anchorMap_.emplace_back("section_" + std::to_string(self->sectionCounter_ - 1), self->pagesCreated_);
  } else if (strcmp(localName, "title") == 0) {
    self->inTitle_ = true;
    self->boldUntilDepth_ = std::min(self->boldUntilDepth_, self->depth_);
    self->flushPartWordBuffer();
    if (self->currentTextBlock_ && !self->currentTextBlock_->isEmpty()) {
      self->makePages();
    }
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);
  } else if (strcmp(localName, "subtitle") == 0) {
    self->inSubtitle_ = true;
    self->boldUntilDepth_ = std::min(self->boldUntilDepth_, self->depth_);
    self->flushPartWordBuffer();
    if (self->currentTextBlock_ && !self->currentTextBlock_->isEmpty()) {
      self->makePages();
    }
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);
  } else if (strcmp(localName, "p") == 0) {
    self->inParagraph_ = true;
    if (!self->currentTextBlock_) {
      TextBlock::BLOCK_STYLE style = self->inTitle_ || self->inSubtitle_
                                         ? TextBlock::CENTER_ALIGN
                                         : static_cast<TextBlock::BLOCK_STYLE>(self->config_.paragraphAlignment);
      self->startNewTextBlock(style);
    }
  } else if (strcmp(localName, "emphasis") == 0) {
    self->italicUntilDepth_ = std::min(self->italicUntilDepth_, self->depth_);
  } else if (strcmp(localName, "strong") == 0) {
    self->boldUntilDepth_ = std::min(self->boldUntilDepth_, self->depth_);
  } else if (strcmp(localName, "empty-line") == 0) {
    self->flushPartWordBuffer();
    if (self->currentTextBlock_ && !self->currentTextBlock_->isEmpty()) {
      self->makePages();
    }
    self->addVerticalSpacing(1);
  } else if (strcmp(localName, "image") == 0) {
    // Skip images in v1
  }

  self->depth_++;
}

void XMLCALL Fb2Parser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<Fb2Parser*>(userData);
  const char* localName = stripNamespace(name);

  self->depth_--;

  // Check bold/italic depth AFTER decrementing (depth_ now matches startElement's value)
  if (self->depth_ <= self->boldUntilDepth_) {
    self->boldUntilDepth_ = INT_MAX;
  }
  if (self->depth_ <= self->italicUntilDepth_) {
    self->italicUntilDepth_ = INT_MAX;
  }

  if (self->skipUntilDepth_ == self->depth_) {
    self->skipUntilDepth_ = INT_MAX;
    return;
  }

  if (!self->inBody_) {
    if (strcmp(localName, "body") == 0) {
      // Closing body tag — nothing more to do
    }
    return;
  }

  if (strcmp(localName, "body") == 0) {
    self->inBody_ = false;
    return;
  }

  if (strcmp(localName, "title") == 0) {
    self->inTitle_ = false;
    self->flushPartWordBuffer();
    if (self->currentTextBlock_ && !self->currentTextBlock_->isEmpty()) {
      self->makePages();
    }
    self->addVerticalSpacing(1);
  } else if (strcmp(localName, "subtitle") == 0) {
    self->inSubtitle_ = false;
    self->flushPartWordBuffer();
    if (self->currentTextBlock_ && !self->currentTextBlock_->isEmpty()) {
      self->makePages();
    }
    self->addVerticalSpacing(1);
  } else if (strcmp(localName, "p") == 0) {
    self->inParagraph_ = false;
    self->flushPartWordBuffer();
    if (self->currentTextBlock_ && !self->currentTextBlock_->isEmpty()) {
      self->makePages();
    }
  }
}

void XMLCALL Fb2Parser::characterData(void* userData, const XML_Char* s, int len) {
  auto* self = static_cast<Fb2Parser*>(userData);

  if (self->skipUntilDepth_ < self->depth_) return;
  if (!self->inBody_) return;

  for (int i = 0; i < len; i++) {
    if (isWhitespace(s[i])) {
      if (self->partWordBufferIndex_ > 0) {
        self->flushPartWordBuffer();
      }
      continue;
    }

    if (self->partWordBufferIndex_ >= MAX_WORD_SIZE) {
      self->flushPartWordBuffer();
    }

    self->partWordBuffer_[self->partWordBufferIndex_++] = s[i];
  }
}

void Fb2Parser::flushPartWordBuffer() {
  if (!currentTextBlock_ || partWordBufferIndex_ == 0) {
    partWordBufferIndex_ = 0;
    return;
  }

  partWordBuffer_[partWordBufferIndex_] = '\0';
  partWordBufferIndex_ = static_cast<int>(utf8NormalizeNfc(partWordBuffer_, partWordBufferIndex_));
  currentTextBlock_->addWord(partWordBuffer_, getCurrentFontFamily());
  partWordBufferIndex_ = 0;
}

void Fb2Parser::startNewTextBlock(TextBlock::BLOCK_STYLE style) {
  if (currentTextBlock_) {
    if (currentTextBlock_->isEmpty()) {
      currentTextBlock_->setStyle(style);
      return;
    }
    makePages();
  }
  currentTextBlock_.reset(new ParsedText(style, config_.indentLevel, config_.hyphenation, true, isRtl_));
}

void Fb2Parser::makePages() {
  if (!currentTextBlock_ || currentTextBlock_->isEmpty()) return;

  flushPartWordBuffer();

  if (!currentPage_) {
    startNewPage();
  }

  const int lineHeight = static_cast<int>(renderer_.getLineHeight(config_.fontId) * config_.lineCompression);
  bool continueProcessing = true;

  currentTextBlock_->layoutAndExtractLines(renderer_, config_.fontId, config_.viewportWidth,
                                           [this, &continueProcessing](const std::shared_ptr<TextBlock>& line) {
                                             if (!continueProcessing) return;
                                             addLineToPage(line);
                                             if (hitMaxPages_) {
                                               continueProcessing = false;
                                             }
                                           });

  // Paragraph spacing (same pattern as PlainTextParser/ChapterHtmlSlimParser)
  if (!hitMaxPages_) {
    switch (config_.spacingLevel) {
      case 1:
        currentPageNextY_ += lineHeight / 4;
        break;
      case 3:
        currentPageNextY_ += lineHeight;
        break;
    }
  }

  currentTextBlock_.reset();
}

void Fb2Parser::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int lineHeight = static_cast<int>(renderer_.getLineHeight(config_.fontId) * config_.lineCompression);

  if (!currentPage_) {
    startNewPage();
  }

  if (currentPageNextY_ + lineHeight > config_.viewportHeight) {
    onPageComplete_(std::move(currentPage_));
    pagesCreated_++;
    startNewPage();

    if (maxPages_ > 0 && pagesCreated_ >= maxPages_) {
      hitMaxPages_ = true;
      stopRequested_ = true;
    }
  }

  currentPage_->elements.push_back(std::make_shared<PageLine>(line, 0, currentPageNextY_));
  currentPageNextY_ += lineHeight;
}

void Fb2Parser::startNewPage() {
  currentPage_.reset(new Page());
  currentPageNextY_ = 0;
}

EpdFontFamily::Style Fb2Parser::getCurrentFontFamily() const {
  bool bold = (boldUntilDepth_ < INT_MAX);
  bool italic = (italicUntilDepth_ < INT_MAX);
  if (bold && italic) return EpdFontFamily::BOLD_ITALIC;
  if (bold) return EpdFontFamily::BOLD;
  if (italic) return EpdFontFamily::ITALIC;
  return EpdFontFamily::REGULAR;
}

void Fb2Parser::addVerticalSpacing(int lines) {
  const int lineHeight = static_cast<int>(renderer_.getLineHeight(config_.fontId) * config_.lineCompression);
  currentPageNextY_ += lineHeight * lines;
}
