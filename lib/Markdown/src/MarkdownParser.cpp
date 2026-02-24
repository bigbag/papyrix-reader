/**
 * MarkdownParser.cpp
 *
 * Markdown parser implementation using md_parser tokenization.
 * Reads directly from SD card with minimal memory usage.
 */

#include "MarkdownParser.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Page.h>
#include <ParsedText.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <blocks/TextBlock.h>
#include <esp_heap_caps.h>

#include <utility>

#include "md_parser.h"

#define TAG "MD_PARSE"

namespace {
bool isWhitespaceChar(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }
}  // namespace

MarkdownParser::MarkdownParser(std::string filepath, GfxRenderer& renderer, const RenderConfig& config)
    : filepath_(std::move(filepath)), renderer_(renderer), config_(config) {
  lineBuffer_[0] = '\0';
}

MarkdownParser::~MarkdownParser() = default;

void MarkdownParser::reset() {
  currentOffset_ = 0;
  hasMore_ = true;
  isRtl_ = false;
}

int MarkdownParser::getCurrentFontStyle(const ParseContext& ctx) const {
  if (ctx.inBold && ctx.inItalic) {
    return EpdFontFamily::BOLD_ITALIC;
  } else if (ctx.inBold) {
    return EpdFontFamily::BOLD;
  } else if (ctx.inItalic) {
    return EpdFontFamily::ITALIC;
  }
  return EpdFontFamily::REGULAR;
}

void MarkdownParser::flushWordBuffer(ParseContext& ctx) {
  if (ctx.wordBufferIndex > 0) {
    ctx.wordBuffer[ctx.wordBufferIndex] = '\0';
    ctx.wordBufferIndex = utf8NormalizeNfc(ctx.wordBuffer, ctx.wordBufferIndex);
    if (ctx.textBlock) {
      ctx.textBlock->addWord(ctx.wordBuffer, static_cast<EpdFontFamily::Style>(getCurrentFontStyle(ctx)));
    }
    ctx.wordBufferIndex = 0;
  }
}

void MarkdownParser::startNewTextBlock(ParseContext& ctx, const int style) {
  if (ctx.textBlock) {
    if (ctx.textBlock->isEmpty()) {
      ctx.textBlock->setStyle(static_cast<TextBlock::BLOCK_STYLE>(style));
      return;
    }
    flushTextBlock(ctx);
  }
  ctx.textBlock.reset(new ParsedText(static_cast<TextBlock::BLOCK_STYLE>(style), config_.indentLevel,
                                     config_.hyphenation, true, isRtl_));
}

void MarkdownParser::flushTextBlock(ParseContext& ctx) {
  flushWordBuffer(ctx);
  if (!ctx.textBlock || ctx.textBlock->isEmpty()) {
    return;
  }

  if (!ctx.currentPage) {
    ctx.currentPage.reset(new Page());
    ctx.pageNextY = 0;
  }

  const int lineHeight = static_cast<int>(renderer_.getLineHeight(config_.fontId) * config_.lineCompression);

  ctx.textBlock->layoutAndExtractLines(renderer_, config_.fontId, config_.viewportWidth,
                                       [this, &ctx](const std::shared_ptr<TextBlock>& textBlock) {
                                         if (!ctx.hitMaxPages) {
                                           addLineToPage(ctx, textBlock);
                                         }
                                       });

  ctx.textBlock.reset();

  switch (config_.spacingLevel) {
    case 1:
      ctx.pageNextY += lineHeight / 4;
      break;
    case 3:
      ctx.pageNextY += lineHeight;
      break;
  }
}

bool MarkdownParser::addLineToPage(ParseContext& ctx, std::shared_ptr<TextBlock> line) {
  const int lineHeight = static_cast<int>(renderer_.getLineHeight(config_.fontId) * config_.lineCompression);

  if (ctx.pageNextY + lineHeight > config_.viewportHeight) {
    if (ctx.onPageComplete) {
      const size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
      LOG_DBG(TAG, "Page %d complete, heap: %zu free", ctx.pagesCreated, freeHeap);
      ctx.onPageComplete(std::move(ctx.currentPage));
      ctx.pagesCreated++;

      if (freeHeap < 12000) {
        LOG_ERR(TAG, "Stopping early due to low memory (%zu bytes)", freeHeap);
        ctx.hitMaxPages = true;
        ctx.currentPage.reset(new Page());
        ctx.pageNextY = 0;
        return false;
      }
    }
    ctx.currentPage.reset(new Page());
    ctx.pageNextY = 0;

    if (ctx.maxPages > 0 && ctx.pagesCreated >= ctx.maxPages) {
      ctx.hitMaxPages = true;
      return false;
    }
  }

  ctx.currentPage->elements.push_back(std::make_shared<PageLine>(line, 0, ctx.pageNextY));
  ctx.pageNextY += lineHeight;
  return true;
}

bool MarkdownParser::readLine(FsFile& file) {
  int i = 0;
  bool readAnyChar = false;
  while (i < LINE_BUFFER_SIZE - 1) {
    int c = file.read();
    if (c < 0) break;
    readAnyChar = true;
    if (c == '\n') break;
    if (c == '\r') continue;
    lineBuffer_[i++] = static_cast<char>(c);
  }
  lineBuffer_[i] = '\0';
  return readAnyChar;
}

bool MarkdownParser::tokenCallback(const md_token_t* token, void* userData) {
  auto& ctx = *static_cast<ParseContext*>(userData);
  auto* self = ctx.self;

  if (ctx.hitMaxPages) {
    return false;
  }

  switch (token->type) {
    case MD_TEXT: {
      // Process text character by character to split into words
      for (uint16_t i = 0; i < token->length; i++) {
        char c = token->text[i];
        if (isWhitespaceChar(c)) {
          self->flushWordBuffer(ctx);
        } else {
          if (ctx.wordBufferIndex >= MAX_WORD_SIZE) {
            ctx.wordBuffer[ctx.wordBufferIndex] = '\0';
            if (ctx.textBlock) {
              ctx.textBlock->addWord(ctx.wordBuffer, static_cast<EpdFontFamily::Style>(self->getCurrentFontStyle(ctx)));
            }
            ctx.wordBufferIndex = 0;
          }
          ctx.wordBuffer[ctx.wordBufferIndex++] = c;
        }
      }
      break;
    }

    case MD_HEADER_START: {
      self->flushTextBlock(ctx);
      ctx.headerLevel = token->data;
      self->startNewTextBlock(ctx, TextBlock::CENTER_ALIGN);
      ctx.inBold = true;
      break;
    }

    case MD_HEADER_END: {
      self->flushTextBlock(ctx);
      ctx.inBold = false;
      ctx.headerLevel = 0;
      break;
    }

    case MD_BOLD_START:
      self->flushWordBuffer(ctx);
      ctx.inBold = true;
      break;

    case MD_BOLD_END:
      self->flushWordBuffer(ctx);
      ctx.inBold = false;
      break;

    case MD_ITALIC_START:
      self->flushWordBuffer(ctx);
      ctx.inItalic = true;
      break;

    case MD_ITALIC_END:
      self->flushWordBuffer(ctx);
      ctx.inItalic = false;
      break;

    case MD_LIST_ITEM_START: {
      self->flushTextBlock(ctx);
      self->startNewTextBlock(ctx, TextBlock::LEFT_ALIGN);
      if (token->data > 0) {
        // Ordered list - emit number
        char numBuf[8];
        snprintf(numBuf, sizeof(numBuf), "%d.", token->data);
        ctx.textBlock->addWord(numBuf, EpdFontFamily::REGULAR);
      } else {
        // Unordered list - emit bullet
        ctx.textBlock->addWord("•", EpdFontFamily::REGULAR);
      }
      break;
    }

    case MD_CODE_INLINE: {
      self->flushWordBuffer(ctx);
      // Emit inline code in italic
      bool savedItalic = ctx.inItalic;
      ctx.inItalic = true;
      for (uint16_t i = 0; i < token->length; i++) {
        char c = token->text[i];
        if (isWhitespaceChar(c)) {
          self->flushWordBuffer(ctx);
        } else if (ctx.wordBufferIndex < MAX_WORD_SIZE) {
          ctx.wordBuffer[ctx.wordBufferIndex++] = c;
        }
      }
      self->flushWordBuffer(ctx);
      ctx.inItalic = savedItalic;
      break;
    }

    case MD_CODE_BLOCK_START: {
      self->flushTextBlock(ctx);
      self->startNewTextBlock(ctx, TextBlock::LEFT_ALIGN);
      ctx.textBlock->addWord("[Code:", EpdFontFamily::ITALIC);
      ctx.inCodeBlock = true;
      break;
    }

    case MD_CODE_BLOCK_END: {
      if (ctx.textBlock) {
        ctx.textBlock->addWord("...]", EpdFontFamily::ITALIC);
      }
      self->flushTextBlock(ctx);
      ctx.inCodeBlock = false;
      break;
    }

    case MD_HR: {
      self->flushTextBlock(ctx);
      self->startNewTextBlock(ctx, TextBlock::CENTER_ALIGN);
      ctx.textBlock->addWord("───────────", EpdFontFamily::REGULAR);
      self->flushTextBlock(ctx);
      break;
    }

    case MD_BLOCKQUOTE_START: {
      self->flushTextBlock(ctx);
      self->startNewTextBlock(ctx, TextBlock::LEFT_ALIGN);
      ctx.inItalic = true;
      break;
    }

    case MD_BLOCKQUOTE_END:
      self->flushTextBlock(ctx);
      ctx.inItalic = false;
      break;

    case MD_LINK_TEXT_START:
    case MD_LINK_TEXT_END:
    case MD_LINK_URL:
      // Just emit link text, ignore URL (handled via MD_TEXT between LINK_TEXT_START/END)
      break;

    case MD_IMAGE_ALT_START:
    case MD_IMAGE_ALT_END:
    case MD_IMAGE_URL: {
      // Show placeholder for images
      if (token->type == MD_IMAGE_ALT_START) {
        self->flushWordBuffer(ctx);
        if (ctx.textBlock) {
          ctx.textBlock->addWord("[Image]", EpdFontFamily::ITALIC);
        }
      }
      break;
    }

    case MD_NEWLINE: {
      // Newline within a block - just add space
      self->flushWordBuffer(ctx);
      break;
    }

    case MD_STRIKE_START:
    case MD_STRIKE_END:
    case MD_LIST_ITEM_END:
    case MD_PARAGRAPH_START:
    case MD_PARAGRAPH_END:
      // Not used in this implementation
      break;
  }

  return true;
}

bool MarkdownParser::parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages,
                                const AbortCallback& shouldAbort) {
  FsFile file;
  if (!SdMan.openFileForRead("MD", filepath_, file)) {
    LOG_ERR(TAG, "Failed to open file: %s", filepath_.c_str());
    return false;
  }

  fileSize_ = file.size();
  if (fileSize_ == 0) {
    LOG_ERR(TAG, "Empty markdown file");
    file.close();
    hasMore_ = false;
    return true;
  }

  file.seekSet(currentOffset_);

  if (currentOffset_ == 0 && !isRtl_) {
    if (readLine(file)) {
      isRtl_ = ScriptDetector::containsArabic(lineBuffer_);
    }
    file.seekSet(currentOffset_);
  }

  LOG_INF(TAG, "Parsing from offset %zu, file size %zu", currentOffset_, fileSize_);
  LOG_DBG(TAG, "Heap: %zu free", heap_caps_get_free_size(MALLOC_CAP_8BIT));

  // Initialize parsing context
  ParseContext ctx{};
  ctx.self = this;
  ctx.pageNextY = 0;
  ctx.inBold = false;
  ctx.inItalic = false;
  ctx.inCodeBlock = false;
  ctx.headerLevel = 0;
  ctx.hitMaxPages = false;
  ctx.pagesCreated = 0;
  ctx.maxPages = maxPages;
  ctx.onPageComplete = onPageComplete;
  ctx.wordBufferIndex = 0;

  // Initialize md_parser
  md_parser_t parser;
  md_parser_init(&parser, tokenCallback, &ctx);

  // Start with a paragraph block
  startNewTextBlock(ctx, config_.paragraphAlignment);

  size_t bytesProcessed = 0;
  bool prevLineBlank = true;
  uint16_t abortCheckCounter = 0;

  while (!ctx.hitMaxPages) {
    // Check for external abort every few lines
    if (shouldAbort && (++abortCheckCounter % 20 == 0) && shouldAbort()) {
      LOG_INF(TAG, "Aborted by external request");
      ctx.hitMaxPages = true;
      break;
    }

    if (!readLine(file)) {
      break;
    }
    bytesProcessed = file.position() - currentOffset_;

    int lineLen = strlen(lineBuffer_);

    // Check for blank lines - they separate blocks
    bool isBlank = true;
    for (int i = 0; i < lineLen; i++) {
      if (!isWhitespaceChar(lineBuffer_[i])) {
        isBlank = false;
        break;
      }
    }

    if (isBlank) {
      if (!prevLineBlank && !ctx.inCodeBlock) {
        flushTextBlock(ctx);
        startNewTextBlock(ctx, config_.paragraphAlignment);
      }
      prevLineBlank = true;
      continue;
    }

    // Reset parser state for each line but preserve formatting state
    md_parser_reset(&parser);

    // Parse the line
    md_parse(&parser, lineBuffer_, lineLen);

    prevLineBlank = false;

    // Periodic memory check
    if (ctx.textBlock && ctx.textBlock->size() > 300) {
      const size_t freeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      if (freeBlock < 25000) {
        LOG_ERR(TAG, "Low memory (%zu free), flushing early", freeBlock);
        ctx.textBlock->layoutAndExtractLines(
            renderer_, config_.fontId, config_.viewportWidth,
            [this, &ctx](const std::shared_ptr<TextBlock>& textBlock) {
              if (!ctx.hitMaxPages) {
                addLineToPage(ctx, textBlock);
              }
            },
            false);
      }
    }
  }

  file.close();

  // Finalize
  flushTextBlock(ctx);
  if (ctx.currentPage && !ctx.currentPage->elements.empty() && onPageComplete) {
    onPageComplete(std::move(ctx.currentPage));
    ctx.pagesCreated++;
  }

  currentOffset_ += bytesProcessed;
  hasMore_ = ctx.hitMaxPages || (currentOffset_ < fileSize_);

  LOG_INF(TAG, "Parsed %d pages, offset %zu/%zu, hasMore=%d", ctx.pagesCreated, currentOffset_, fileSize_, hasMore_);
  return true;
}
