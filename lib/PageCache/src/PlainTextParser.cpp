#include "PlainTextParser.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Page.h>
#include <ParsedText.h>
#include <SDCardManager.h>
#include <Utf8.h>

#define TAG "TXT_PARSE"

#include <utility>

namespace {
constexpr size_t READ_CHUNK_SIZE = 4096;

bool isWhitespace(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }
}  // namespace

PlainTextParser::PlainTextParser(std::string filepath, GfxRenderer& renderer, const RenderConfig& config)
    : filepath_(std::move(filepath)), renderer_(renderer), config_(config) {}

void PlainTextParser::reset() {
  currentOffset_ = 0;
  hasMore_ = true;
  isRtl_ = false;
}

bool PlainTextParser::parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages,
                                 const AbortCallback& shouldAbort) {
  FsFile file;
  if (!SdMan.openFileForRead("TXT", filepath_, file)) {
    LOG_ERR(TAG, "Failed to open file: %s", filepath_.c_str());
    return false;
  }

  fileSize_ = file.size();
  if (currentOffset_ > 0) {
    file.seek(currentOffset_);
  }

  const int lineHeight = static_cast<int>(renderer_.getLineHeight(config_.fontId) * config_.lineCompression);
  const int maxLinesPerPage = config_.viewportHeight / lineHeight;

  uint8_t buffer[READ_CHUNK_SIZE + 1];
  std::unique_ptr<ParsedText> currentBlock;
  std::unique_ptr<Page> currentPage;
  int16_t currentPageY = 0;
  uint16_t pagesCreated = 0;
  std::string partialWord;
  uint16_t abortCheckCounter = 0;

  auto startNewPage = [&]() {
    currentPage.reset(new Page());
    currentPageY = 0;
  };

  auto addLineToPage = [&](std::shared_ptr<TextBlock> line) {
    if (!currentPage) {
      startNewPage();
    }

    if (currentPageY + lineHeight > config_.viewportHeight) {
      onPageComplete(std::move(currentPage));
      pagesCreated++;
      startNewPage();

      if (maxPages > 0 && pagesCreated >= maxPages) {
        return false;
      }
    }

    currentPage->elements.push_back(std::make_shared<PageLine>(line, 0, currentPageY));
    currentPageY += lineHeight;
    return true;
  };

  auto flushBlock = [&]() -> bool {
    if (!currentBlock || currentBlock->isEmpty()) return true;

    bool continueProcessing = true;
    currentBlock->layoutAndExtractLines(renderer_, config_.fontId, config_.viewportWidth,
                                        [&](const std::shared_ptr<TextBlock>& line) {
                                          if (!continueProcessing) return;
                                          if (!addLineToPage(line)) {
                                            continueProcessing = false;
                                          }
                                        });

    currentBlock.reset();
    return continueProcessing;
  };

  if (currentOffset_ == 0) {
    size_t peekBytes = file.read(buffer, READ_CHUNK_SIZE);
    if (peekBytes > 0) {
      buffer[peekBytes] = '\0';
      isRtl_ = ScriptDetector::containsArabic(reinterpret_cast<const char*>(buffer));
    }
    file.seekSet(0);
  }

  startNewPage();
  currentBlock.reset(new ParsedText(static_cast<TextBlock::BLOCK_STYLE>(config_.paragraphAlignment),
                                    config_.indentLevel, config_.hyphenation, true, isRtl_));

  while (file.available() > 0) {
    // Check for abort every few iterations
    if (shouldAbort && (++abortCheckCounter % 10 == 0) && shouldAbort()) {
      LOG_INF(TAG, "Aborted by external request");
      currentOffset_ = file.position();
      hasMore_ = true;
      file.close();
      return false;
    }

    size_t bytesRead = file.read(buffer, READ_CHUNK_SIZE);
    if (bytesRead == 0) break;

    buffer[bytesRead] = '\0';

    for (size_t i = 0; i < bytesRead; i++) {
      char c = static_cast<char>(buffer[i]);

      // Handle newlines as paragraph breaks
      if (c == '\n') {
        // Flush partial word
        if (!partialWord.empty()) {
          partialWord.resize(utf8NormalizeNfc(&partialWord[0], partialWord.size()));
          currentBlock->addWord(partialWord, EpdFontFamily::REGULAR);
          partialWord.clear();
        }

        // Flush current block (paragraph)
        if (!flushBlock()) {
          currentOffset_ = file.position() - (bytesRead - i - 1);
          hasMore_ = true;
          file.close();

          // Complete final page if it has content
          if (currentPage && !currentPage->elements.empty()) {
            onPageComplete(std::move(currentPage));
          }
          return true;
        }

        // Start new paragraph
        currentBlock.reset(new ParsedText(static_cast<TextBlock::BLOCK_STYLE>(config_.paragraphAlignment),
                                          config_.indentLevel, config_.hyphenation, true, isRtl_));

        // Add paragraph spacing
        switch (config_.spacingLevel) {
          case 1:
            currentPageY += lineHeight / 4;
            break;
          case 3:
            currentPageY += lineHeight;
            break;
        }
        continue;
      }

      if (isWhitespace(c)) {
        if (!partialWord.empty()) {
          partialWord.resize(utf8NormalizeNfc(&partialWord[0], partialWord.size()));
          currentBlock->addWord(partialWord, EpdFontFamily::REGULAR);
          partialWord.clear();
        }
        continue;
      }

      partialWord += c;

      // Prevent extremely long words from accumulating
      if (partialWord.length() > 100) {
        partialWord.resize(utf8NormalizeNfc(&partialWord[0], partialWord.size()));
        currentBlock->addWord(partialWord, EpdFontFamily::REGULAR);
        partialWord.clear();
      }
    }

    // Check if we hit max pages
    if (maxPages > 0 && pagesCreated >= maxPages) {
      currentOffset_ = file.position();
      hasMore_ = (currentOffset_ < fileSize_);
      file.close();
      return true;
    }
  }

  // Flush remaining content
  if (!partialWord.empty()) {
    partialWord.resize(utf8NormalizeNfc(&partialWord[0], partialWord.size()));
    currentBlock->addWord(partialWord, EpdFontFamily::REGULAR);
  }
  flushBlock();

  // Complete final page
  if (currentPage && !currentPage->elements.empty()) {
    onPageComplete(std::move(currentPage));
    pagesCreated++;
  }

  file.close();
  currentOffset_ = fileSize_;
  hasMore_ = false;

  LOG_INF(TAG, "Parsed %d pages from %s", pagesCreated, filepath_.c_str());
  return true;
}
