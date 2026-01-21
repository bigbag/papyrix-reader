/**
 * MarkdownParser.cpp
 *
 * Markdown parser implementation using MD4C
 */

#include "MarkdownParser.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include "../md4c/md4c.h"
#include "Epub/Page.h"

namespace {
bool isWhitespaceChar(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }
}  // namespace

EpdFontFamily::Style MarkdownParser::getCurrentFontStyle() const {
  if (boldDepth > 0 && italicDepth > 0) {
    return EpdFontFamily::BOLD_ITALIC;
  } else if (boldDepth > 0) {
    return EpdFontFamily::BOLD;
  } else if (italicDepth > 0) {
    return EpdFontFamily::ITALIC;
  }
  return EpdFontFamily::REGULAR;
}

void MarkdownParser::flushPartWordBuffer() {
  if (partWordBufferIndex > 0) {
    partWordBuffer[partWordBufferIndex] = '\0';
    if (currentTextBlock) {
      currentTextBlock->addWord(partWordBuffer, getCurrentFontStyle());
    }
    partWordBufferIndex = 0;
  }
}

void MarkdownParser::startNewTextBlock(const TextBlock::BLOCK_STYLE style) {
  if (currentTextBlock) {
    // Already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      currentTextBlock->setStyle(style);
      return;
    }

    makePages();
  }
  currentTextBlock.reset(new ParsedText(style, config.indentLevel, config.hyphenation));
}

void MarkdownParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int lineHeight = renderer.getLineHeight(config.fontId) * config.lineCompression;

  if (currentPageNextY + lineHeight > config.viewportHeight) {
    completePageFn(std::move(currentPage));
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  currentPage->elements.push_back(std::make_shared<PageLine>(line, 0, currentPageNextY));
  currentPageNextY += lineHeight;
}

void MarkdownParser::makePages() {
  if (!currentTextBlock) {
    Serial.printf("[%lu] [MDP] !! No text block to make pages for !!\n", millis());
    return;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  const int lineHeight = renderer.getLineHeight(config.fontId) * config.lineCompression;
  currentTextBlock->layoutAndExtractLines(
      renderer, config.fontId, config.viewportWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); });

  // Extra paragraph spacing based on spacingLevel (0=none, 1=small, 3=large)
  switch (config.spacingLevel) {
    case 1:
      currentPageNextY += lineHeight / 4;  // Small (1/4 line)
      break;
    case 3:
      currentPageNextY += lineHeight;  // Large (full line)
      break;
  }
}

int MarkdownParser::enterBlockCallback(int blockType, void* detail, void* userdata) {
  auto* self = static_cast<MarkdownParser*>(userdata);

  switch (static_cast<MD_BLOCKTYPE>(blockType)) {
    case MD_BLOCK_DOC:
      // Start of document - initialize first text block
      self->startNewTextBlock(static_cast<TextBlock::BLOCK_STYLE>(self->config.paragraphAlignment));
      break;

    case MD_BLOCK_H: {
      // Flush any pending word
      self->flushPartWordBuffer();
      // Headers are centered and bold
      auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
      self->headerLevel = h->level;
      self->startNewTextBlock(TextBlock::CENTER_ALIGN);
      self->boldDepth++;
      break;
    }

    case MD_BLOCK_P:
      // Flush any pending word
      self->flushPartWordBuffer();
      // Normal paragraph
      self->startNewTextBlock(static_cast<TextBlock::BLOCK_STYLE>(self->config.paragraphAlignment));
      break;

    case MD_BLOCK_QUOTE:
      // Blockquote - use italic for differentiation
      self->flushPartWordBuffer();
      self->startNewTextBlock(TextBlock::LEFT_ALIGN);
      self->italicDepth++;
      break;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
      // Lists - nothing special at list start
      break;

    case MD_BLOCK_LI:
      // List item - add bullet prefix
      self->flushPartWordBuffer();
      self->startNewTextBlock(TextBlock::LEFT_ALIGN);
      self->inListItem = true;
      self->firstListItemWord = true;
      break;

    case MD_BLOCK_CODE:
      // Code block - add placeholder
      self->flushPartWordBuffer();
      self->startNewTextBlock(TextBlock::LEFT_ALIGN);
      // Code blocks are rendered with a placeholder since we can't use monospace
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("[Code:", EpdFontFamily::ITALIC);
      }
      break;

    case MD_BLOCK_HR:
      // Horizontal rule - add visual separator
      self->flushPartWordBuffer();
      self->startNewTextBlock(TextBlock::CENTER_ALIGN);
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("───────────", EpdFontFamily::REGULAR);
      }
      break;

    case MD_BLOCK_TABLE:
      // Tables - add placeholder
      self->flushPartWordBuffer();
      self->startNewTextBlock(TextBlock::CENTER_ALIGN);
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("[Table", EpdFontFamily::ITALIC);
        self->currentTextBlock->addWord("omitted]", EpdFontFamily::ITALIC);
      }
      break;

    case MD_BLOCK_HTML:
      // Raw HTML - skip
      break;

    default:
      break;
  }

  return 0;
}

int MarkdownParser::leaveBlockCallback(int blockType, void* detail, void* userdata) {
  auto* self = static_cast<MarkdownParser*>(userdata);
  (void)detail;

  switch (static_cast<MD_BLOCKTYPE>(blockType)) {
    case MD_BLOCK_DOC:
      // End of document
      break;

    case MD_BLOCK_H:
      // End of header
      self->flushPartWordBuffer();
      if (self->boldDepth > 0) self->boldDepth--;
      self->headerLevel = 0;
      break;

    case MD_BLOCK_P:
    case MD_BLOCK_LI:
      // End of paragraph or list item
      self->flushPartWordBuffer();
      self->inListItem = false;
      self->firstListItemWord = false;
      break;

    case MD_BLOCK_QUOTE:
      // End of blockquote
      self->flushPartWordBuffer();
      if (self->italicDepth > 0) self->italicDepth--;
      break;

    case MD_BLOCK_CODE:
      // End of code block
      self->flushPartWordBuffer();
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("]", EpdFontFamily::ITALIC);
      }
      break;

    default:
      break;
  }

  return 0;
}

int MarkdownParser::enterSpanCallback(int spanType, void* detail, void* userdata) {
  auto* self = static_cast<MarkdownParser*>(userdata);
  (void)detail;

  switch (static_cast<MD_SPANTYPE>(spanType)) {
    case MD_SPAN_STRONG:
      self->boldDepth++;
      break;

    case MD_SPAN_EM:
      self->italicDepth++;
      break;

    case MD_SPAN_CODE:
      // Inline code - use italic
      self->italicDepth++;
      break;

    case MD_SPAN_A:
      // Links - underline not supported, just show text normally
      break;

    case MD_SPAN_IMG:
      // Images - add placeholder
      self->flushPartWordBuffer();
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("[Image]", EpdFontFamily::ITALIC);
      }
      break;

    case MD_SPAN_DEL:
      // Strikethrough - not supported, just show text
      break;

    default:
      break;
  }

  return 0;
}

int MarkdownParser::leaveSpanCallback(int spanType, void* detail, void* userdata) {
  auto* self = static_cast<MarkdownParser*>(userdata);
  (void)detail;

  switch (static_cast<MD_SPANTYPE>(spanType)) {
    case MD_SPAN_STRONG:
      if (self->boldDepth > 0) self->boldDepth--;
      break;

    case MD_SPAN_EM:
      if (self->italicDepth > 0) self->italicDepth--;
      break;

    case MD_SPAN_CODE:
      if (self->italicDepth > 0) self->italicDepth--;
      break;

    default:
      break;
  }

  return 0;
}

int MarkdownParser::textCallback(int textType, const char* text, unsigned size, void* userdata) {
  auto* self = static_cast<MarkdownParser*>(userdata);

  // Handle special text types
  switch (static_cast<MD_TEXTTYPE>(textType)) {
    case MD_TEXT_BR:
    case MD_TEXT_SOFTBR:
      // Line break - flush current word and add space
      self->flushPartWordBuffer();
      return 0;

    case MD_TEXT_CODE:
      // Code text - just add ellipsis for code blocks
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord("...", EpdFontFamily::ITALIC);
      }
      return 0;

    case MD_TEXT_HTML:
      // Raw HTML - skip
      return 0;

    case MD_TEXT_ENTITY:
      // HTML entities - try to handle common ones
      if (size == 6 && strncmp(text, "&nbsp;", 6) == 0) {
        self->flushPartWordBuffer();
      } else if (self->partWordBufferIndex < MAX_WORD_SIZE) {
        if (size == 6 && strncmp(text, "&quot;", 6) == 0) {
          self->partWordBuffer[self->partWordBufferIndex++] = '"';
        } else if (size == 5 && strncmp(text, "&amp;", 5) == 0) {
          self->partWordBuffer[self->partWordBufferIndex++] = '&';
        } else if (size == 4 && strncmp(text, "&lt;", 4) == 0) {
          self->partWordBuffer[self->partWordBufferIndex++] = '<';
        } else if (size == 4 && strncmp(text, "&gt;", 4) == 0) {
          self->partWordBuffer[self->partWordBufferIndex++] = '>';
        }
      }
      return 0;

    default:
      break;
  }

  // Add bullet for first word in list item
  if (self->firstListItemWord && self->inListItem) {
    if (self->currentTextBlock) {
      self->currentTextBlock->addWord("•", EpdFontFamily::REGULAR);
    }
    self->firstListItemWord = false;
  }

  EpdFontFamily::Style fontStyle = self->getCurrentFontStyle();

  // Process text character by character
  for (unsigned i = 0; i < size; i++) {
    if (isWhitespaceChar(text[i])) {
      // Whitespace - flush word buffer
      if (self->partWordBufferIndex > 0) {
        self->partWordBuffer[self->partWordBufferIndex] = '\0';
        if (self->currentTextBlock) {
          self->currentTextBlock->addWord(self->partWordBuffer, fontStyle);
        }
        self->partWordBufferIndex = 0;
      }
      continue;
    }

    // If buffer is full, flush it
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      self->partWordBuffer[self->partWordBufferIndex] = '\0';
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord(self->partWordBuffer, fontStyle);
      }
      self->partWordBufferIndex = 0;
    }

    self->partWordBuffer[self->partWordBufferIndex++] = text[i];
  }

  // If we have > 750 words buffered up, perform layout to free memory
  if (self->currentTextBlock && self->currentTextBlock->size() > 750) {
    Serial.printf("[%lu] [MDP] Text block too long, splitting into multiple pages\n", millis());
    self->currentTextBlock->layoutAndExtractLines(
        self->renderer, self->config.fontId, self->config.viewportWidth,
        [self](const std::shared_ptr<TextBlock>& textBlock) { self->addLineToPage(textBlock); }, false);
  }

  return 0;
}

bool MarkdownParser::parseAndBuildPages() {
  if (!markdown || !markdown->isLoaded()) {
    Serial.printf("[%lu] [MDP] Markdown not loaded\n", millis());
    return false;
  }

  const size_t fileSize = markdown->getFileSize();
  if (fileSize == 0) {
    Serial.printf("[%lu] [MDP] Empty markdown file\n", millis());
    return false;
  }

  // Allocate buffer to read entire file (or in chunks for very large files)
  uint8_t* buffer = static_cast<uint8_t*>(malloc(fileSize + 1));
  if (!buffer) {
    Serial.printf("[%lu] [MDP] Failed to allocate buffer for %zu bytes\n", millis(), fileSize);
    return false;
  }

  // Read file content
  size_t bytesRead = markdown->readContent(buffer, 0, fileSize);
  if (bytesRead != fileSize) {
    Serial.printf("[%lu] [MDP] Only read %zu of %zu bytes\n", millis(), bytesRead, fileSize);
    free(buffer);
    return false;
  }
  buffer[bytesRead] = '\0';

  Serial.printf("[%lu] [MDP] Read %zu bytes of markdown\n", millis(), bytesRead);

  // Initialize parser state
  boldDepth = 0;
  italicDepth = 0;
  headerLevel = 0;
  inListItem = false;
  firstListItemWord = false;
  partWordBufferIndex = 0;
  currentTextBlock.reset();
  currentPage.reset();
  currentPageNextY = 0;

  // Setup MD4C parser
  MD_PARSER parser = {};
  parser.abi_version = 0;
  parser.flags = MD_DIALECT_COMMONMARK;  // Use CommonMark for basic markdown
  parser.enter_block = reinterpret_cast<int (*)(MD_BLOCKTYPE, void*, void*)>(enterBlockCallback);
  parser.leave_block = reinterpret_cast<int (*)(MD_BLOCKTYPE, void*, void*)>(leaveBlockCallback);
  parser.enter_span = reinterpret_cast<int (*)(MD_SPANTYPE, void*, void*)>(enterSpanCallback);
  parser.leave_span = reinterpret_cast<int (*)(MD_SPANTYPE, void*, void*)>(leaveSpanCallback);
  parser.text = reinterpret_cast<int (*)(MD_TEXTTYPE, const MD_CHAR*, MD_SIZE, void*)>(textCallback);
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  // Parse markdown
  int result = md_parse(reinterpret_cast<const char*>(buffer), static_cast<unsigned>(bytesRead), &parser, this);

  free(buffer);

  if (result != 0) {
    Serial.printf("[%lu] [MDP] md_parse failed with code %d\n", millis(), result);
    return false;
  }

  // Process any remaining content
  flushPartWordBuffer();
  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    makePages();
  }
  if (currentPage) {
    completePageFn(std::move(currentPage));
  }

  Serial.printf("[%lu] [MDP] Parsing complete\n", millis());
  return true;
}
