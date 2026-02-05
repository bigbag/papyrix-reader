#include "ChapterHtmlSlimParser.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <ImageConverter.h>
#include <SDCardManager.h>
#include <esp_heap_caps.h>
#include <expat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../Page.h"

const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr int NUM_HEADER_TAGS = sizeof(HEADER_TAGS) / sizeof(HEADER_TAGS[0]);

// Minimum file size (in bytes) to show progress bar - smaller chapters don't benefit from it
constexpr size_t MIN_SIZE_FOR_PROGRESS = 50 * 1024;  // 50KB

const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote"};
constexpr int NUM_BLOCK_TAGS = sizeof(BLOCK_TAGS) / sizeof(BLOCK_TAGS[0]);

const char* BOLD_TAGS[] = {"b", "strong"};
constexpr int NUM_BOLD_TAGS = sizeof(BOLD_TAGS) / sizeof(BOLD_TAGS[0]);

const char* ITALIC_TAGS[] = {"i", "em"};
constexpr int NUM_ITALIC_TAGS = sizeof(ITALIC_TAGS) / sizeof(ITALIC_TAGS[0]);

const char* IMAGE_TAGS[] = {"img"};
constexpr int NUM_IMAGE_TAGS = sizeof(IMAGE_TAGS) / sizeof(IMAGE_TAGS[0]);

const char* SKIP_TAGS[] = {"head"};
constexpr int NUM_SKIP_TAGS = sizeof(SKIP_TAGS) / sizeof(SKIP_TAGS[0]);

bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

// given the start and end of a tag, check to see if it matches a known tag
bool matches(const char* tag_name, const char* possible_tags[], const int possible_tag_count) {
  for (int i = 0; i < possible_tag_count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

void ChapterHtmlSlimParser::flushPartWordBuffer() {
  if (!currentTextBlock || partWordBufferIndex == 0) {
    partWordBufferIndex = 0;
    return;
  }

  // Determine font style from HTML tags and CSS
  const bool isBold = boldUntilDepth < depth || cssBoldUntilDepth < depth;
  const bool isItalic = italicUntilDepth < depth || cssItalicUntilDepth < depth;

  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (isBold && isItalic) {
    fontStyle = EpdFontFamily::BOLD_ITALIC;
  } else if (isBold) {
    fontStyle = EpdFontFamily::BOLD;
  } else if (isItalic) {
    fontStyle = EpdFontFamily::ITALIC;
  }

  partWordBuffer[partWordBufferIndex] = '\0';
  currentTextBlock->addWord(partWordBuffer, fontStyle);
  partWordBufferIndex = 0;
}

// start a new text block if needed
void ChapterHtmlSlimParser::startNewTextBlock(const TextBlock::BLOCK_STYLE style) {
  if (currentTextBlock) {
    // already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      currentTextBlock->setStyle(style);
      return;
    }

    makePages();
  }
  currentTextBlock.reset(new ParsedText(style, config.indentLevel, config.hyphenation));
}

void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  (void)atts;

  // Prevent stack overflow from deeply nested XML
  if (self->depth >= MAX_XML_DEPTH) {
    XML_StopParser(self->xmlParser_, XML_FALSE);
    return;
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    std::string srcAttr;
    std::string altText;
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "src") == 0 && atts[i + 1][0] != '\0') {
          srcAttr = atts[i + 1];
        } else if (strcmp(atts[i], "alt") == 0 && atts[i + 1][0] != '\0') {
          altText = atts[i + 1];
        }
      }
    }

    Serial.printf("[%lu] [EHP] Found image: src=%s\n", millis(), srcAttr.empty() ? "(empty)" : srcAttr.c_str());

    // Try to cache and display the image if we have image support configured
    if (!srcAttr.empty() && self->readItemFn && !self->imageCachePath.empty()) {
      std::string cachedPath = self->cacheImage(srcAttr);
      if (!cachedPath.empty()) {
        // Read image dimensions from cached BMP
        FsFile bmpFile;
        if (SdMan.openFileForRead("EHP", cachedPath, bmpFile)) {
          Bitmap bitmap(bmpFile, false);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            Serial.printf("[%lu] [EHP] Image loaded: %dx%d\n", millis(), bitmap.getWidth(), bitmap.getHeight());
            auto imageBlock = std::make_shared<ImageBlock>(cachedPath, bitmap.getWidth(), bitmap.getHeight());
            bmpFile.close();

            // Flush any pending text block before adding image
            if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
              self->makePages();
            }

            self->addImageToPage(imageBlock);
            self->depth += 1;
            return;
          } else {
            Serial.printf("[%lu] [EHP] BMP parse failed for cached image\n", millis());
          }
          bmpFile.close();
        } else {
          Serial.printf("[%lu] [EHP] Failed to open cached BMP: %s\n", millis(), cachedPath.c_str());
        }
      }
    } else {
      Serial.printf("[%lu] [EHP] Image skipped: src=%d, readItemFn=%d, imageCachePath=%d\n", millis(), !srcAttr.empty(),
                    self->readItemFn != nullptr, !self->imageCachePath.empty());
    }

    // Fallback: show placeholder with alt text if image processing failed
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);
    if (self->currentTextBlock) {
      if (!altText.empty()) {
        std::string placeholder = "[Image: " + altText + "]";
        self->currentTextBlock->addWord(placeholder.c_str(), EpdFontFamily::ITALIC);
      } else {
        self->currentTextBlock->addWord("[Image]", EpdFontFamily::ITALIC);
      }
    }

    self->depth += 1;
    return;
  }

  // Special handling for tables - show placeholder text instead of dropping silently
  // TODO: Render tables - parse table structure (thead, tbody, tr, td, th), calculate
  // column widths, handle colspan/rowspan, and render as formatted text grid.
  if (strcmp(name, "table") == 0) {
    // For now, add placeholder text
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);
    if (self->currentTextBlock) {
      self->currentTextBlock->addWord("[Table omitted]", EpdFontFamily::ITALIC);
    }

    // Skip table contents
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  if (matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    // start skip
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // Skip blocks with role="doc-pagebreak" and epub:type="pagebreak"
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0 ||
          strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }
    }
  }

  // Skip empty anchor tags with aria-hidden (Pandoc line number anchors)
  // These appear as: <a href="#cb1-1" aria-hidden="true" tabindex="-1"></a>
  if (strcmp(name, "a") == 0 && atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "aria-hidden") == 0 && strcmp(atts[i + 1], "true") == 0) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }
    }
  }

  // Extract class and style attributes for CSS lookup
  std::string classAttr;
  std::string styleAttr;
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0) {
        classAttr = atts[i + 1];
      } else if (strcmp(atts[i], "style") == 0) {
        styleAttr = atts[i + 1];
      }
    }
  }

  // Query CSS for combined style (tag + classes + inline)
  CssStyle cssStyle;
  if (self->cssParser_) {
    cssStyle = self->cssParser_->getCombinedStyle(name, classAttr);
  }
  // Inline styles override stylesheet rules (static method, no instance needed)
  if (!styleAttr.empty()) {
    cssStyle.merge(CssParser::parseInlineStyle(styleAttr));
  }

  // Apply CSS font-weight and font-style
  if (cssStyle.hasFontWeight && cssStyle.fontWeight == CssFontWeight::Bold) {
    self->cssBoldUntilDepth = min(self->cssBoldUntilDepth, self->depth);
  }
  if (cssStyle.hasFontStyle && cssStyle.fontStyle == CssFontStyle::Italic) {
    self->cssItalicUntilDepth = min(self->cssItalicUntilDepth, self->depth);
  }

  if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);
    self->boldUntilDepth = min(self->boldUntilDepth, self->depth);
  } else if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
    if (strcmp(name, "br") == 0) {
      self->flushPartWordBuffer();
      const auto style = self->currentTextBlock ? self->currentTextBlock->getStyle()
                                                : static_cast<TextBlock::BLOCK_STYLE>(self->config.paragraphAlignment);
      self->startNewTextBlock(style);
    } else {
      // Determine block style: CSS text-align takes precedence
      TextBlock::BLOCK_STYLE blockStyle = static_cast<TextBlock::BLOCK_STYLE>(self->config.paragraphAlignment);
      if (cssStyle.hasTextAlign) {
        switch (cssStyle.textAlign) {
          case TextAlign::Left:
            blockStyle = TextBlock::LEFT_ALIGN;
            break;
          case TextAlign::Right:
            blockStyle = TextBlock::RIGHT_ALIGN;
            break;
          case TextAlign::Center:
            blockStyle = TextBlock::CENTER_ALIGN;
            break;
          case TextAlign::Justify:
            blockStyle = TextBlock::JUSTIFIED;
            break;
          default:
            break;
        }
      }
      self->startNewTextBlock(blockStyle);
    }
  } else if (matches(name, BOLD_TAGS, NUM_BOLD_TAGS)) {
    self->boldUntilDepth = min(self->boldUntilDepth, self->depth);
  } else if (matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
    self->italicUntilDepth = min(self->italicUntilDepth, self->depth);
  }

  self->depth += 1;
}

void XMLCALL ChapterHtmlSlimParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  // Zero Width No-Break Space / BOM (U+FEFF) = 0xEF 0xBB 0xBF
  const XML_Char FEFF_BYTE_1 = static_cast<XML_Char>(0xEF);
  const XML_Char FEFF_BYTE_2 = static_cast<XML_Char>(0xBB);
  const XML_Char FEFF_BYTE_3 = static_cast<XML_Char>(0xBF);

  for (int i = 0; i < len; i++) {
    if (isWhitespace(s[i])) {
      // Currently looking at whitespace, if there's anything in the partWordBuffer, flush it
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      // Skip the whitespace char
      continue;
    }

    // Skip BOM character (sometimes appears before em-dashes in EPUBs)
    if (s[i] == FEFF_BYTE_1) {
      // Check if the next two bytes complete the 3-byte sequence
      if ((i + 2 < len) && (s[i + 1] == FEFF_BYTE_2) && (s[i + 2] == FEFF_BYTE_3)) {
        // Sequence 0xEF 0xBB 0xBF found!
        i += 2;    // Skip the next two bytes
        continue;  // Move to the next iteration
      }
    }

    // If we're about to run out of space, then cut the word off and start a new one
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      self->flushPartWordBuffer();
    }

    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }

  // If we have > 750 words buffered up, perform the layout and consume out all but the last line
  // There should be enough here to build out 1-2 full pages and doing this will free up a lot of
  // memory.
  // Spotted when reading Intermezzo, there are some really long text blocks in there.
  if (self->currentTextBlock && self->currentTextBlock->size() > 750) {
    Serial.printf("[%lu] [EHP] Text block too long, splitting into multiple pages\n", millis());
    self->currentTextBlock->layoutAndExtractLines(
        self->renderer, self->config.fontId, self->config.viewportWidth,
        [self](const std::shared_ptr<TextBlock>& textBlock) { self->addLineToPage(textBlock); }, false);
  }
}

void XMLCALL ChapterHtmlSlimParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  (void)name;

  if (self->partWordBufferIndex > 0) {
    // Only flush out part word buffer if we're closing a block tag or are at the top of the HTML file.
    // We don't want to flush out content when closing inline tags like <span>.
    // Currently this also flushes out on closing <b> and <i> tags, but they are line tags so that shouldn't happen,
    // text styling needs to be overhauled to fix it.
    const bool shouldBreakText =
        matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS) || matches(name, HEADER_TAGS, NUM_HEADER_TAGS) ||
        matches(name, BOLD_TAGS, NUM_BOLD_TAGS) || matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS) || self->depth == 1;

    if (shouldBreakText) {
      self->flushPartWordBuffer();
    }
  }

  self->depth -= 1;

  if (self->skipUntilDepth == self->depth) {
    self->skipUntilDepth = INT_MAX;
  }
  if (self->boldUntilDepth == self->depth) {
    self->boldUntilDepth = INT_MAX;
  }
  if (self->italicUntilDepth == self->depth) {
    self->italicUntilDepth = INT_MAX;
  }
  if (self->cssBoldUntilDepth == self->depth) {
    self->cssBoldUntilDepth = INT_MAX;
  }
  if (self->cssItalicUntilDepth == self->depth) {
    self->cssItalicUntilDepth = INT_MAX;
  }
}

bool ChapterHtmlSlimParser::shouldAbort() const {
  // Check external abort callback first (cooperative cancellation)
  if (externalAbortCallback_ && externalAbortCallback_()) {
    Serial.printf("[%lu] [EHP] External abort requested\n", millis());
    return true;
  }

  // Check timeout
  if (millis() - parseStartTime_ > MAX_PARSE_TIME_MS) {
    Serial.printf("[%lu] [EHP] Parse timeout exceeded (%u ms)\n", millis(), MAX_PARSE_TIME_MS);
    return true;
  }

  // Check memory pressure
  const size_t freeHeap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (freeHeap < MIN_FREE_HEAP) {
    Serial.printf("[%lu] [EHP] Low memory (%zu bytes free)\n", millis(), freeHeap);
    return true;
  }

  return false;
}

bool ChapterHtmlSlimParser::parseAndBuildPages() {
  parseStartTime_ = millis();
  loopCounter_ = 0;
  dataUriStripper_.reset();
  startNewTextBlock(static_cast<TextBlock::BLOCK_STYLE>(config.paragraphAlignment));

  const XML_Parser parser = XML_ParserCreate(nullptr);
  xmlParser_ = parser;  // Store for stopping mid-parse
  int done;

  if (!parser) {
    Serial.printf("[%lu] [EHP] Couldn't allocate memory for parser\n", millis());
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("EHP", filepath, file)) {
    XML_ParserFree(parser);
    return false;
  }

  // Get file size for progress calculation
  const size_t totalSize = file.size();
  size_t bytesRead = 0;
  int lastProgress = -1;
  pagesCreated_ = 0;

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);

  do {
    // Periodic safety check and yield
    if (++loopCounter_ % YIELD_CHECK_INTERVAL == 0) {
      if (shouldAbort()) {
        Serial.printf("[%lu] [EHP] Aborting parse, pages created: %u\n", millis(), pagesCreated_);
        break;
      }
      vTaskDelay(1);  // Yield to prevent watchdog reset
    }

    void* const buf = XML_GetBuffer(parser, 1024);
    if (!buf) {
      Serial.printf("[%lu] [EHP] Couldn't allocate memory for buffer\n", millis());
      XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
      XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      xmlParser_ = nullptr;
      file.close();
      currentPage.reset();
      currentTextBlock.reset();
      return false;
    }

    size_t len = file.read(static_cast<uint8_t*>(buf), 1024);

    if (len == 0) {
      Serial.printf("[%lu] [EHP] File read error\n", millis());
      XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
      XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      xmlParser_ = nullptr;
      file.close();
      currentPage.reset();
      currentTextBlock.reset();
      return false;
    }

    // Strip data URIs BEFORE expat parses the buffer to prevent OOM on large embedded images.
    // This replaces src="data:image/..." with src="#" so expat never sees the huge base64 string.
    const size_t originalLen = len;
    len = dataUriStripper_.strip(static_cast<char*>(buf), len, 1024);

    // Update progress (call every 10% change to avoid too frequent updates)
    // Only show progress for larger chapters where rendering overhead is worth it
    bytesRead += originalLen;
    if (progressFn && totalSize >= MIN_SIZE_FOR_PROGRESS) {
      const int progress = static_cast<int>((bytesRead * 100) / totalSize);
      if (lastProgress / 10 != progress / 10) {
        lastProgress = progress;
        progressFn(progress);
      }
    }

    done = file.available() == 0;

    if (XML_ParseBuffer(parser, static_cast<int>(len), done) == XML_STATUS_ERROR) {
      Serial.printf("[%lu] [EHP] Parse error at line %lu:\n%s\n", millis(), XML_GetCurrentLineNumber(parser),
                    XML_ErrorString(XML_GetErrorCode(parser)));
      XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
      XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      xmlParser_ = nullptr;
      file.close();
      currentPage.reset();
      currentTextBlock.reset();
      return false;
    }
  } while (!done);

  XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
  XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
  XML_SetCharacterDataHandler(parser, nullptr);
  XML_ParserFree(parser);
  xmlParser_ = nullptr;
  file.close();

  // Process last page if there is still text
  if (currentTextBlock && !stopRequested_) {
    makePages();
    if (!stopRequested_ && currentPage) {
      completePageFn(std::move(currentPage));
    }
    currentPage.reset();
    currentTextBlock.reset();
  }

  return true;
}

void ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  if (stopRequested_) return;

  const int lineHeight = renderer.getLineHeight(config.fontId) * config.lineCompression;

  if (currentPageNextY + lineHeight > config.viewportHeight) {
    ++pagesCreated_;
    if (!completePageFn(std::move(currentPage))) {
      stopRequested_ = true;
      if (xmlParser_) {
        XML_StopParser(xmlParser_, XML_FALSE);
      }
      return;
    }
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  currentPage->elements.push_back(std::make_shared<PageLine>(line, 0, currentPageNextY));
  currentPageNextY += lineHeight;
}

void ChapterHtmlSlimParser::makePages() {
  if (!currentTextBlock) {
    Serial.printf("[%lu] [EHP] !! No text block to make pages for !!\n", millis());
    return;
  }

  // Check memory before expensive layout operation
  const size_t freeHeap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (freeHeap < MIN_FREE_HEAP * 2) {
    Serial.printf("[%lu] [EHP] Insufficient memory for layout (%zu bytes)\n", millis(), freeHeap);
    currentTextBlock.reset();
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

std::string ChapterHtmlSlimParser::cacheImage(const std::string& src) {
  // Skip data URIs - embedded base64 images can't be extracted and waste memory
  if (src.length() >= 5 && strncasecmp(src.c_str(), "data:", 5) == 0) {
    Serial.printf("[%lu] [EHP] Skipping embedded data URI image\n", millis());
    return "";
  }

  // Skip remaining images after too many consecutive failures
  if (consecutiveImageFailures_ >= MAX_CONSECUTIVE_IMAGE_FAILURES) {
    Serial.printf("[%lu] [EHP] Skipping image - too many failures\n", millis());
    return "";
  }

  // Resolve relative path from chapter base
  std::string resolvedPath = FsHelpers::normalisePath(chapterBasePath + src);

  // Generate cache filename from hash
  size_t srcHash = std::hash<std::string>{}(resolvedPath);
  std::string cachedBmpPath = imageCachePath + "/" + std::to_string(srcHash) + ".bmp";

  // Check if already cached
  if (SdMan.exists(cachedBmpPath.c_str())) {
    consecutiveImageFailures_ = 0;  // Reset on success
    return cachedBmpPath;
  }

  // Check for failed marker
  std::string failedMarker = imageCachePath + "/" + std::to_string(srcHash) + ".failed";
  if (SdMan.exists(failedMarker.c_str())) {
    consecutiveImageFailures_++;
    return "";
  }

  // Check if format is supported
  if (!ImageConverterFactory::isSupported(src)) {
    Serial.printf("[%lu] [EHP] Unsupported image format: %s\n", millis(), src.c_str());
    FsFile marker;
    if (SdMan.openFileForWrite("EHP", failedMarker, marker)) {
      marker.close();
    }
    consecutiveImageFailures_++;
    return "";
  }

  // Extract image to temp file (include hash in name for uniqueness)
  const std::string tempExt = FsHelpers::isPngFile(src) ? ".png" : ".jpg";
  std::string tempPath = imageCachePath + "/.tmp_" + std::to_string(srcHash) + tempExt;
  FsFile tempFile;
  if (!SdMan.openFileForWrite("EHP", tempPath, tempFile)) {
    Serial.printf("[%lu] [EHP] Failed to create temp file for image\n", millis());
    return "";
  }

  if (!readItemFn(resolvedPath, tempFile, 1024)) {
    Serial.printf("[%lu] [EHP] Failed to extract image: %s\n", millis(), resolvedPath.c_str());
    tempFile.close();
    SdMan.remove(tempPath.c_str());
    FsFile marker;
    if (SdMan.openFileForWrite("EHP", failedMarker, marker)) {
      marker.close();
    }
    consecutiveImageFailures_++;
    return "";
  }
  tempFile.close();

  // Max width is viewport, max height is half of viewport to avoid images taking too much vertical space
  const int maxImageHeight = config.viewportHeight / 2;
  ImageConvertConfig convertConfig;
  convertConfig.maxWidth = static_cast<int>(config.viewportWidth);
  convertConfig.maxHeight = maxImageHeight;
  convertConfig.logTag = "EHP";

  const bool success = ImageConverterFactory::convertToBmp(tempPath, cachedBmpPath, convertConfig);
  SdMan.remove(tempPath.c_str());

  if (!success) {
    Serial.printf("[%lu] [EHP] Failed to convert image to BMP: %s\n", millis(), resolvedPath.c_str());
    SdMan.remove(cachedBmpPath.c_str());
    FsFile marker;
    if (SdMan.openFileForWrite("EHP", failedMarker, marker)) {
      marker.close();
    }
    consecutiveImageFailures_++;
    return "";
  }

  consecutiveImageFailures_ = 0;  // Reset on success
  Serial.printf("[%lu] [EHP] Cached image: %s\n", millis(), cachedBmpPath.c_str());
  return cachedBmpPath;
}

void ChapterHtmlSlimParser::addImageToPage(std::shared_ptr<ImageBlock> image) {
  if (stopRequested_) return;

  const int imageHeight = image->getHeight();
  const int lineHeight = renderer.getLineHeight(config.fontId) * config.lineCompression;

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  // Check if image fits on current page
  if (currentPageNextY + imageHeight > config.viewportHeight) {
    if (!completePageFn(std::move(currentPage))) {
      stopRequested_ = true;
      if (xmlParser_) {
        XML_StopParser(xmlParser_, XML_FALSE);
      }
      return;
    }
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  // Center image horizontally (cast to signed to handle images wider than viewport)
  int xPos = (static_cast<int>(config.viewportWidth) - static_cast<int>(image->getWidth())) / 2;
  if (xPos < 0) xPos = 0;

  currentPage->elements.push_back(std::make_shared<PageImage>(image, xPos, currentPageNextY));
  currentPageNextY += imageHeight + lineHeight;  // Add line spacing after image
}
