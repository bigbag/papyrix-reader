#include "PlainTextParser.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Page.h>
#include <ParsedText.h>
#include <SDCardManager.h>
#include <Utf8.h>

#define TAG "TXT_PARSE"

#include <memory>
#include <new>
#include <utility>

namespace {
constexpr size_t READ_CHUNK_SIZE = 4096;

bool isWhitespace(char c) { return c == ' ' || c == '\t'; }
}  // namespace

PlainTextParser::PlainTextParser(std::string filepath, GfxRenderer& renderer, const RenderConfig& config)
    : filepath_(std::move(filepath)), renderer_(renderer), config_(config) {}

void PlainTextParser::reset() {
  currentOffset_ = 0;
  hasMore_ = true;
  isRtl_ = false;
  detectedEncoding_ = Encoding::Utf8;
  encodingTable_ = nullptr;
  bomSkipBytes_ = 0;
  pendingBlock_.reset();
  pendingSpacing_ = 0;
  pendingSawNewline_ = false;
  pendingPartialWord_.clear();
  pendingPage_.reset();
  pendingPageY_ = 0;
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

  const int lineHeight = static_cast<int>(renderer_.getEffectiveLineHeight(config_.fontId) * config_.lineCompression);
  const int maxLinesPerPage = config_.viewportHeight / lineHeight;

  // Keep the 4 KB read buffer off the stack: the foreground "page not cached"
  // render runs this whole parse -> layout -> font-render chain on the 8 KB
  // loopTask stack, where an on-stack buffer this size overflows it (Issue #137).
  auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[READ_CHUNK_SIZE + 1]);
  if (!buffer) {
    LOG_ERR(TAG, "Failed to allocate read buffer");
    file.close();
    return false;
  }
  std::unique_ptr<ParsedText> currentBlock;
  std::unique_ptr<Page> currentPage;
  int16_t currentPageY = 0;
  uint16_t pagesCreated = 0;
  std::string partialWord = std::move(pendingPartialWord_);
  uint16_t abortCheckCounter = 0;
  bool sawNewline = pendingSawNewline_;
  pendingSawNewline_ = false;

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
        currentPage->elements.push_back(std::make_shared<PageLine>(line, 0, currentPageY));
        currentPageY += lineHeight;
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
    currentBlock->layoutAndExtractLines(
        renderer_, config_.fontId, config_.viewportWidth,
        [&](const std::shared_ptr<TextBlock>& line) {
          if (!continueProcessing) return;
          if (!addLineToPage(line)) {
            continueProcessing = false;
          }
        },
        true,
        [&continueProcessing, &shouldAbort]() -> bool {
          return !continueProcessing || (shouldAbort && shouldAbort());
        });

    if (continueProcessing) {
      currentBlock.reset();
    }
    // else: currentBlock still has unconsumed words — preserve it
    return continueProcessing;
  };

  // Lay out the current block as a soft (mid-paragraph) break, used to bound the
  // per-block working set. Returns true if the batch filled up — resume state is
  // persisted and the caller must return; returns false if the block was fully
  // laid out and parsing can continue with a fresh block.
  auto softFlushAtCap = [&](size_t resumeOffset) -> bool {
    if (flushBlock()) {
      return false;
    }
    pendingBlock_ = std::move(currentBlock);
    pendingPartialWord_ = std::move(partialWord);
    pendingSawNewline_ = sawNewline;
    currentOffset_ = resumeOffset;
    hasMore_ = true;
    if (currentPage && !currentPage->elements.empty()) {
      pendingPage_ = std::move(currentPage);
      pendingPageY_ = currentPageY;
    }
    file.close();
    return true;
  };

  if (currentOffset_ == 0) {
    size_t peekBytes = file.read(buffer.get(), READ_CHUNK_SIZE);
    if (peekBytes > 0) {
      buffer[peekBytes] = '\0';
      detectedEncoding_ = detectEncoding(buffer.get(), peekBytes, bomSkipBytes_);
      encodingTable_ = getEncodingTable(detectedEncoding_);
    }
    file.seekSet(bomSkipBytes_);
  }

  auto addWordWithRtlCheck = [&](const std::string& word, EpdFontFamily::Style style) {
    if (!isRtl_ && ScriptDetector::classify(word.c_str()) == ScriptDetector::Script::ARABIC) {
      isRtl_ = true;
      currentBlock->setRtl(true);
    }
    currentBlock->addWord(word, style);
  };

  // Resume: restore partial page from previous batch
  if (pendingPage_) {
    currentPage = std::move(pendingPage_);
    currentPageY = pendingPageY_;
    pendingPageY_ = 0;
  } else {
    startNewPage();
  }

  // Resume: flush any pending block carried over from a previous interrupted batch
  if (pendingBlock_) {
    currentBlock = std::move(pendingBlock_);
    if (!flushBlock()) {
      pendingBlock_ = std::move(currentBlock);
      pendingPartialWord_ = std::move(partialWord);
      pendingSawNewline_ = sawNewline;
      if (currentPage && !currentPage->elements.empty()) {
        pendingPage_ = std::move(currentPage);
        pendingPageY_ = currentPageY;
      }
      file.close();
      return true;
    }
  }

  if (pendingSpacing_ > 0) {
    currentPageY += pendingSpacing_;
    pendingSpacing_ = 0;
  }

  if (!currentBlock) {
    currentBlock.reset(new ParsedText(static_cast<TextBlock::BLOCK_STYLE>(config_.paragraphAlignment),
                                      config_.indentLevel, config_.hyphenation, true, isRtl_));
  }

  while (file.available() > 0) {
    // Check for abort every few iterations
    if (shouldAbort && (++abortCheckCounter % 10 == 0) && shouldAbort()) {
      LOG_INF(TAG, "Aborted by external request");
      pendingSawNewline_ = sawNewline;
      currentOffset_ = file.position();
      hasMore_ = true;
      file.close();
      return false;
    }

    size_t bytesRead = file.read(buffer.get(), READ_CHUNK_SIZE);
    if (bytesRead == 0) break;

    buffer[bytesRead] = '\0';

    for (size_t i = 0; i < bytesRead; i++) {
      // Bound the working set: split an over-long paragraph mid-stream instead of
      // accumulating thousands of words into one block (Issue #137).
      if (currentBlock && currentBlock->size() >= ParsedText::kMaxWordsPerBlock) {
        if (softFlushAtCap(file.position() - (bytesRead - i))) {
          return true;
        }
        if (!currentBlock) {
          currentBlock.reset(new ParsedText(static_cast<TextBlock::BLOCK_STYLE>(config_.paragraphAlignment),
                                            config_.indentLevel, config_.hyphenation, true, isRtl_));
        }
      }

      char c = static_cast<char>(buffer[i]);

      if (c == '\r') continue;

      if (c == '\n') {
        if (!partialWord.empty()) {
          partialWord.resize(utf8NormalizeNfc(&partialWord[0], partialWord.size()));
          addWordWithRtlCheck(partialWord, EpdFontFamily::REGULAR);
          partialWord.clear();
        }

        if (sawNewline) {
          if (!flushBlock()) {
            switch (config_.spacingLevel) {
              case 1:
                pendingSpacing_ = lineHeight / 4;
                break;
              case 3:
                pendingSpacing_ = lineHeight;
                break;
            }
            pendingBlock_ = std::move(currentBlock);
            pendingSawNewline_ = true;
            currentOffset_ = file.position() - (bytesRead - i - 1);
            hasMore_ = true;
            file.close();

            if (currentPage && !currentPage->elements.empty()) {
              pendingPage_ = std::move(currentPage);
              pendingPageY_ = currentPageY;
            }
            return true;
          }

          if (!currentBlock) {
            currentBlock.reset(new ParsedText(static_cast<TextBlock::BLOCK_STYLE>(config_.paragraphAlignment),
                                              config_.indentLevel, config_.hyphenation, true, isRtl_));

            switch (config_.spacingLevel) {
              case 1:
                currentPageY += lineHeight / 4;
                break;
              case 3:
                currentPageY += lineHeight;
                break;
            }
          }
        } else {
          sawNewline = true;
        }
        continue;
      }

      if (isWhitespace(c)) {
        if (!partialWord.empty()) {
          partialWord.resize(utf8NormalizeNfc(&partialWord[0], partialWord.size()));
          addWordWithRtlCheck(partialWord, EpdFontFamily::REGULAR);
          partialWord.clear();
        }
        continue;
      }

      sawNewline = false;

      if (encodingTable_ && static_cast<uint8_t>(c) >= 0x80) {
        int cp = encodingTable_[static_cast<uint8_t>(c) - 128];
        if (cp > 0) {
          char utf8Buf[4];
          int len = codepointToUtf8(cp, utf8Buf);
          partialWord.append(utf8Buf, len);
        }
      } else {
        partialWord += c;
      }

      // Prevent extremely long words from accumulating
      if (partialWord.length() > 100) {
        // Back up to last valid UTF-8 codepoint boundary to avoid splitting multi-byte chars
        size_t safeLen = partialWord.length();
        while (safeLen > 0 && (static_cast<unsigned char>(partialWord[safeLen - 1]) & 0xC0) == 0x80) {
          safeLen--;
        }
        if (safeLen > 0 && static_cast<unsigned char>(partialWord[safeLen - 1]) >= 0xC0) {
          safeLen--;
        }

        if (safeLen > 0) {
          std::string overflow = partialWord.substr(safeLen);
          partialWord.resize(safeLen);
          partialWord.resize(utf8NormalizeNfc(&partialWord[0], partialWord.size()));
          addWordWithRtlCheck(partialWord, EpdFontFamily::REGULAR);
          partialWord = std::move(overflow);
        } else {
          partialWord.resize(utf8NormalizeNfc(&partialWord[0], partialWord.size()));
          addWordWithRtlCheck(partialWord, EpdFontFamily::REGULAR);
          partialWord.clear();
        }
      }
    }

    // Check if we hit max pages
    if (maxPages > 0 && pagesCreated >= maxPages) {
      pendingSawNewline_ = sawNewline;
      pendingPartialWord_ = std::move(partialWord);
      if (currentBlock && !currentBlock->isEmpty()) {
        pendingBlock_ = std::move(currentBlock);
      }
      if (currentPage && !currentPage->elements.empty()) {
        pendingPage_ = std::move(currentPage);
        pendingPageY_ = currentPageY;
      }
      currentOffset_ = file.position();
      hasMore_ = (currentOffset_ < fileSize_);
      file.close();
      return true;
    }
  }

  // Flush remaining content
  if (!partialWord.empty()) {
    partialWord.resize(utf8NormalizeNfc(&partialWord[0], partialWord.size()));
    addWordWithRtlCheck(partialWord, EpdFontFamily::REGULAR);
  }
  if (!flushBlock()) {
    // The trailing block exceeded this batch — all bytes are consumed, but the
    // remaining lines must be emitted on a later call from the preserved block.
    pendingBlock_ = std::move(currentBlock);
    pendingSawNewline_ = sawNewline;
    if (currentPage && !currentPage->elements.empty()) {
      pendingPage_ = std::move(currentPage);
      pendingPageY_ = currentPageY;
    }
    currentOffset_ = fileSize_;
    hasMore_ = true;
    file.close();
    return true;
  }

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
