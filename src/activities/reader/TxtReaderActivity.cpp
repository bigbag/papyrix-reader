/**
 * TxtReaderActivity.cpp
 *
 * Plain text reader activity implementation
 * Uses 8KB streaming chunks for memory efficiency
 */

#include "TxtReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "config.h"

namespace {
constexpr int pagesPerRefresh = 15;
constexpr unsigned long skipPageMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr int horizontalPadding = 10;
constexpr int verticalPadding = 10;
constexpr int statusBarMargin = 19;
constexpr size_t CHUNK_SIZE = 8192;  // 8KB chunks for streaming
}  // namespace

void TxtReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<TxtReaderActivity*>(param);
  self->displayTaskLoop();
}

void TxtReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!txt) {
    return;
  }

  // Configure screen orientation based on settings
  switch (SETTINGS.orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }

  renderingMutex = xSemaphoreCreateMutex();

  txt->setupCacheDir();

  // Try to load existing page index
  if (!loadPageIndex()) {
    // Need to build index - will be done on first render
    indexBuilt = false;
  }

  // Load saved progress
  loadProgress();

  // Save current TXT as last opened book
  APP_STATE.openEpubPath = txt->getPath();
  APP_STATE.saveToFile();

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&TxtReaderActivity::taskTrampoline, "TxtReaderActivityTask",
              6144,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void TxtReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  // Wait until not rendering to delete task
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  txt.reset();
}

void TxtReaderActivity::loop() {
  // Pass input responsibility to sub activity if exists
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Long press BACK (1s+) goes directly to home
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    onGoHome();
    return;
  }

  // Short press BACK goes to file selection
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    onGoBack();
    return;
  }

  const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                            (SETTINGS.shortPwrBtn == CrossPointSettings::PWRBTN_PAGE_TURN &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power)) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right);

  if (!prevReleased && !nextReleased) {
    return;
  }

  if (pageIndex.empty()) {
    return;
  }

  const uint32_t maxPage = pageIndex.size();

  // Handle end of book
  if (currentPage >= maxPage) {
    currentPage = maxPage - 1;
    updateRequired = true;
    return;
  }

  const bool skipPages = mappedInput.getHeldTime() > skipPageMs;
  const int skipAmount = skipPages ? 10 : 1;

  if (prevReleased) {
    if (currentPage >= static_cast<uint32_t>(skipAmount)) {
      currentPage -= skipAmount;
    } else {
      currentPage = 0;
    }
    updateRequired = true;
  } else if (nextReleased) {
    currentPage += skipAmount;
    if (currentPage >= maxPage) {
      currentPage = maxPage;  // Allow showing "End of book"
    }
    updateRequired = true;
  }
}

void TxtReaderActivity::displayTaskLoop() {
  while (true) {
    // If a subactivity is active, yield CPU time but don't render
    if (subActivity) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      renderScreen();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void TxtReaderActivity::renderScreen() {
  // Double-check subactivity under mutex protection
  if (subActivity) {
    return;
  }

  if (!txt) {
    return;
  }

  // Apply screen viewable areas and padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginLeft += horizontalPadding;
  orientedMarginRight += horizontalPadding;
  orientedMarginTop += verticalPadding;
  orientedMarginBottom += statusBarMargin;

  // Build page index if not done yet
  if (!indexBuilt) {
    // Show "Indexing..." message
    renderer.clearScreen(THEME.backgroundColor);
    constexpr int boxMargin = 20;
    const int fontId = SETTINGS.getReaderFontId();
    const int textWidth = renderer.getTextWidth(fontId, "Indexing...");
    const int boxWidth = textWidth + boxMargin * 2;
    const int boxHeight = renderer.getLineHeight(fontId) + boxMargin * 2;
    const int boxX = (renderer.getScreenWidth() - boxWidth) / 2;
    constexpr int boxY = 50;

    renderer.fillRect(boxX, boxY, boxWidth, boxHeight, !THEME.primaryTextBlack);
    renderer.drawText(fontId, boxX + boxMargin, boxY + boxMargin, "Indexing...", THEME.primaryTextBlack);
    renderer.drawRect(boxX + 5, boxY + 5, boxWidth - 10, boxHeight - 10, THEME.primaryTextBlack);
    renderer.displayBuffer();

    if (!buildPageIndex()) {
      renderer.clearScreen(THEME.backgroundColor);
      renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "Failed to index file", THEME.primaryTextBlack, BOLD);
      renderer.displayBuffer();
      return;
    }

    indexBuilt = true;
    savePageIndex();
  }

  // Show end of book screen
  if (currentPage >= pageIndex.size()) {
    renderer.clearScreen(THEME.backgroundColor);
    renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "End of book", THEME.primaryTextBlack, BOLD);
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen(THEME.backgroundColor);
  renderPage();
  saveProgress();
}

void TxtReaderActivity::renderPage() {
  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = renderer.getLineHeight(fontId);

  // Get viewport dimensions
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginLeft += horizontalPadding;
  orientedMarginRight += horizontalPadding;
  orientedMarginTop += verticalPadding;
  orientedMarginBottom += statusBarMargin;

  const int viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;

  // Get page boundaries
  const size_t pageStart = pageIndex[currentPage];
  const size_t pageEnd = (currentPage + 1 < pageIndex.size()) ? pageIndex[currentPage + 1] : txt->getFileSize();
  const size_t pageLen = pageEnd - pageStart;

  // Allocate buffer for this page's content (plus null terminator)
  uint8_t* buffer = static_cast<uint8_t*>(malloc(pageLen + 1));
  if (!buffer) {
    Serial.printf("[%lu] [TXR] Failed to allocate page buffer\n", millis());
    renderer.drawCenteredText(fontId, 300, "Memory error", THEME.primaryTextBlack, BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  // Read page content
  size_t bytesRead = txt->readContent(buffer, pageStart, pageLen);
  buffer[bytesRead] = '\0';

  // Lambda to render text content - can be called multiple times for anti-aliasing passes
  auto renderTextContent = [&]() {
    int y = orientedMarginTop;
    size_t offset = 0;

    while (offset < bytesRead && y + lineHeight <= renderer.getScreenHeight() - orientedMarginBottom) {
      // Find end of current line (newline or wrap point)
      size_t lineStart = offset;
      size_t lineEnd = offset;
      size_t lastWordEnd = offset;
      int lineWidth = 0;

      while (lineEnd < bytesRead) {
        // Handle newline
        if (buffer[lineEnd] == '\n') {
          lineEnd++;
          break;
        }
        if (buffer[lineEnd] == '\r') {
          lineEnd++;
          if (lineEnd < bytesRead && buffer[lineEnd] == '\n') {
            lineEnd++;
          }
          break;
        }

        // Get next UTF-8 character
        size_t charStart = lineEnd;
        size_t charEnd = getNextUtf8Char(buffer, lineEnd, bytesRead);

        // Calculate character width
        char charBuf[8];
        size_t charLen = charEnd - charStart;
        if (charLen < sizeof(charBuf)) {
          memcpy(charBuf, buffer + charStart, charLen);
          charBuf[charLen] = '\0';
          int charWidth = renderer.getTextWidth(fontId, charBuf);

          if (lineWidth + charWidth > viewportWidth) {
            // Line too long - wrap at last word boundary if possible
            if (lastWordEnd > lineStart) {
              lineEnd = lastWordEnd;
            }
            break;
          }

          lineWidth += charWidth;
        }

        // Track word boundaries (after spaces)
        if (buffer[charStart] == ' ' || buffer[charStart] == '\t') {
          lastWordEnd = charEnd;
        }

        lineEnd = charEnd;
      }

      // Render the line
      if (lineEnd > lineStart) {
        size_t renderLen = lineEnd - lineStart;
        // Trim trailing whitespace for rendering
        while (renderLen > 0 &&
               (buffer[lineStart + renderLen - 1] == ' ' || buffer[lineStart + renderLen - 1] == '\t' ||
                buffer[lineStart + renderLen - 1] == '\r' || buffer[lineStart + renderLen - 1] == '\n')) {
          renderLen--;
        }

        if (renderLen > 0) {
          char* lineBuf = static_cast<char*>(malloc(renderLen + 1));
          if (lineBuf) {
            memcpy(lineBuf, buffer + lineStart, renderLen);
            lineBuf[renderLen] = '\0';
            renderer.drawText(fontId, orientedMarginLeft, y, lineBuf, THEME.primaryTextBlack);
            free(lineBuf);
          }
        }
      }

      y += lineHeight;
      offset = lineEnd;
    }
  };

  // Render text content (BW pass)
  renderTextContent();
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  // Display with refresh logic
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(EInkDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getPagesPerRefreshValue();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  // Grayscale text rendering (anti-aliasing)
  if (SETTINGS.textAntiAliasing) {
    renderer.storeBwBuffer();

    // Render LSB grayscale pass
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderTextContent();
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.copyGrayscaleLsbBuffers();

    // Render MSB grayscale pass
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderTextContent();
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.copyGrayscaleMsbBuffers();

    // Display grayscale and restore BW mode
    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.restoreBwBuffer();
  }

  free(buffer);

  Serial.printf("[%lu] [TXR] Rendered page %lu/%zu\n", millis(), currentPage + 1, pageIndex.size());
}

void TxtReaderActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) const {
  const bool showProgress = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;
  const bool showBattery = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                           SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;
  const bool showTitle = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                         SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;

  const auto screenHeight = renderer.getScreenHeight();
  const auto textY = screenHeight - orientedMarginBottom + 2;
  int percentageTextWidth = 0;
  int progressTextWidth = 0;

  if (showProgress && !pageIndex.empty()) {
    const uint8_t bookProgress = (currentPage + 1) * 100 / pageIndex.size();
    char progress[32];
    snprintf(progress, sizeof(progress), "%u/%zu  %u%%", currentPage + 1, pageIndex.size(), bookProgress);
    progressTextWidth = renderer.getTextWidth(THEME.smallFontId, progress);
    renderer.drawText(THEME.smallFontId, renderer.getScreenWidth() - orientedMarginRight - progressTextWidth, textY,
                      progress, THEME.primaryTextBlack);
  }

  if (showBattery) {
    const uint16_t millivolts = battery.readMillivolts();
    char percentageText[8];
    uint16_t percentage = 0;
    if (millivolts < 100) {
      snprintf(percentageText, sizeof(percentageText), "--%%");
    } else {
      percentage = BatteryMonitor::percentageFromMillivolts(millivolts);
      snprintf(percentageText, sizeof(percentageText), "%u%%", percentage);
    }
    percentageTextWidth = renderer.getTextWidth(THEME.smallFontId, percentageText);
    renderer.drawText(THEME.smallFontId, 20 + orientedMarginLeft, textY, percentageText, THEME.primaryTextBlack);

    // Battery icon
    constexpr int batteryWidth = 15;
    constexpr int batteryHeight = 10;
    const int x = orientedMarginLeft;
    const int y = screenHeight - orientedMarginBottom + 5;

    renderer.drawLine(x, y, x + batteryWidth - 4, y, THEME.primaryTextBlack);
    renderer.drawLine(x, y + batteryHeight - 1, x + batteryWidth - 4, y + batteryHeight - 1, THEME.primaryTextBlack);
    renderer.drawLine(x, y, x, y + batteryHeight - 1, THEME.primaryTextBlack);
    renderer.drawLine(x + batteryWidth - 4, y, x + batteryWidth - 4, y + batteryHeight - 1, THEME.primaryTextBlack);
    renderer.drawLine(x + batteryWidth - 3, y + 2, x + batteryWidth - 1, y + 2, THEME.primaryTextBlack);
    renderer.drawLine(x + batteryWidth - 3, y + batteryHeight - 3, x + batteryWidth - 1, y + batteryHeight - 3,
                      THEME.primaryTextBlack);
    renderer.drawLine(x + batteryWidth - 1, y + 2, x + batteryWidth - 1, y + batteryHeight - 3, THEME.primaryTextBlack);

    int filledWidth = percentage * (batteryWidth - 5) / 100 + 1;
    if (filledWidth > batteryWidth - 5) {
      filledWidth = batteryWidth - 5;
    }
    renderer.fillRect(x + 1, y + 1, filledWidth, batteryHeight - 2, THEME.primaryTextBlack);
  }

  if (showTitle) {
    const int batteryAreaWidth = showBattery ? 20 + percentageTextWidth : 0;
    const int titleMarginLeft = batteryAreaWidth + 30 + orientedMarginLeft;
    const int titleMarginRight = progressTextWidth + 30 + orientedMarginRight;
    const int availableTextWidth = renderer.getScreenWidth() - titleMarginLeft - titleMarginRight;

    std::string title = txt->getTitle();
    int titleWidth = renderer.getTextWidth(THEME.smallFontId, title.c_str());
    while (titleWidth > availableTextWidth && title.length() > 11) {
      title.replace(title.length() - 8, 8, "...");
      titleWidth = renderer.getTextWidth(THEME.smallFontId, title.c_str());
    }

    renderer.drawText(THEME.smallFontId, titleMarginLeft + (availableTextWidth - titleWidth) / 2, textY, title.c_str(),
                      THEME.primaryTextBlack);
  }
}

void TxtReaderActivity::saveProgress() const {
  FsFile f;
  if (SdMan.openFileForWrite("TXR", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = (currentPage >> 16) & 0xFF;
    data[3] = (currentPage >> 24) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void TxtReaderActivity::loadProgress() {
  FsFile f;
  if (SdMan.openFileForRead("TXR", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      Serial.printf("[%lu] [TXR] Loaded progress: page %lu\n", millis(), currentPage);

      // Validate against loaded index
      if (indexBuilt && currentPage >= pageIndex.size()) {
        currentPage = 0;
      }
    }
    f.close();
  }
}

bool TxtReaderActivity::loadPageIndex() {
  FsFile f;
  if (!SdMan.openFileForRead("TXR", txt->getCachePath() + "/index.bin", f)) {
    return false;
  }

  // Read header: fileSize (4), viewportWidth (4), linesPerPage (4)
  uint8_t header[12];
  if (f.read(header, 12) != 12) {
    f.close();
    return false;
  }

  cachedFileSize = header[0] | (header[1] << 8) | (header[2] << 16) | (header[3] << 24);
  cachedViewportWidth = header[4] | (header[5] << 8) | (header[6] << 16) | (header[7] << 24);
  cachedLinesPerPage = header[8] | (header[9] << 8) | (header[10] << 16) | (header[11] << 24);

  // Validate cache
  if (!validatePageIndexCache()) {
    f.close();
    return false;
  }

  // Read page count
  uint8_t countBuf[4];
  if (f.read(countBuf, 4) != 4) {
    f.close();
    return false;
  }
  uint32_t pageCount = countBuf[0] | (countBuf[1] << 8) | (countBuf[2] << 16) | (countBuf[3] << 24);

  // Read page offsets
  pageIndex.clear();
  pageIndex.reserve(pageCount);
  for (uint32_t i = 0; i < pageCount; i++) {
    uint8_t offsetBuf[4];
    if (f.read(offsetBuf, 4) != 4) {
      f.close();
      pageIndex.clear();
      return false;
    }
    size_t offset = offsetBuf[0] | (offsetBuf[1] << 8) | (offsetBuf[2] << 16) | (offsetBuf[3] << 24);
    pageIndex.push_back(offset);
  }

  f.close();
  indexBuilt = true;
  Serial.printf("[%lu] [TXR] Loaded page index: %zu pages\n", millis(), pageIndex.size());
  return true;
}

bool TxtReaderActivity::buildPageIndex() {
  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = renderer.getLineHeight(fontId);

  // Get viewport dimensions
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginLeft += horizontalPadding;
  orientedMarginRight += horizontalPadding;
  orientedMarginTop += verticalPadding;
  orientedMarginBottom += statusBarMargin;

  const int viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  const int linesPerPage = viewportHeight / lineHeight;

  // Store cache validation values
  cachedFileSize = txt->getFileSize();
  cachedViewportWidth = viewportWidth;
  cachedLinesPerPage = linesPerPage;

  pageIndex.clear();
  pageIndex.push_back(0);  // First page starts at 0

  const size_t fileSize = txt->getFileSize();
  if (fileSize == 0) {
    Serial.printf("[%lu] [TXR] Empty file, single empty page\n", millis());
    return true;
  }

  uint8_t* buffer = static_cast<uint8_t*>(malloc(CHUNK_SIZE));
  if (!buffer) {
    Serial.printf("[%lu] [TXR] Failed to allocate chunk buffer\n", millis());
    return false;
  }

  int currentLine = 0;
  size_t absoluteOffset = 0;  // Current position in file

  while (absoluteOffset < fileSize) {
    // Read a chunk starting from current position
    size_t toRead = std::min(CHUNK_SIZE, fileSize - absoluteOffset);
    size_t bytesRead = txt->readContent(buffer, absoluteOffset, toRead);
    if (bytesRead == 0) break;

    size_t bufferOffset = 0;

    while (bufferOffset < bytesRead) {
      size_t lineStartInBuffer = bufferOffset;
      size_t lineEndInBuffer = bufferOffset;
      size_t lastWordEndInBuffer = bufferOffset;
      int lineWidth = 0;
      bool foundLineEnd = false;

      while (lineEndInBuffer < bytesRead) {
        // Handle newline
        if (buffer[lineEndInBuffer] == '\n') {
          lineEndInBuffer++;
          foundLineEnd = true;
          break;
        }
        if (buffer[lineEndInBuffer] == '\r') {
          lineEndInBuffer++;
          if (lineEndInBuffer < bytesRead && buffer[lineEndInBuffer] == '\n') {
            lineEndInBuffer++;
          }
          foundLineEnd = true;
          break;
        }

        // Check for incomplete UTF-8 at buffer boundary
        uint8_t firstByte = buffer[lineEndInBuffer];
        size_t expectedCharLen = 1;
        if ((firstByte & 0xE0) == 0xC0)
          expectedCharLen = 2;
        else if ((firstByte & 0xF0) == 0xE0)
          expectedCharLen = 3;
        else if ((firstByte & 0xF8) == 0xF0)
          expectedCharLen = 4;

        if (lineEndInBuffer + expectedCharLen > bytesRead) {
          // Incomplete UTF-8 char - will handle in next chunk read
          break;
        }

        // Get next UTF-8 character
        size_t charStart = lineEndInBuffer;
        size_t charEnd = getNextUtf8Char(buffer, lineEndInBuffer, bytesRead);

        // Calculate character width
        char charBuf[8];
        size_t charLen = charEnd - charStart;
        if (charLen < sizeof(charBuf)) {
          memcpy(charBuf, buffer + charStart, charLen);
          charBuf[charLen] = '\0';
          int charWidth = renderer.getTextWidth(fontId, charBuf);

          if (lineWidth + charWidth > viewportWidth) {
            // Line too long - wrap at word boundary or current position
            if (lastWordEndInBuffer > lineStartInBuffer) {
              lineEndInBuffer = lastWordEndInBuffer;
            }
            foundLineEnd = true;
            break;
          }

          lineWidth += charWidth;
        }

        if (buffer[charStart] == ' ' || buffer[charStart] == '\t') {
          lastWordEndInBuffer = charEnd;
        }

        lineEndInBuffer = charEnd;
      }

      // If we didn't find a line end and there's more file to read,
      // re-read from line start in next iteration
      if (!foundLineEnd && absoluteOffset + bytesRead < fileSize) {
        // Update absoluteOffset to line start and break to re-read
        absoluteOffset += lineStartInBuffer;
        break;
      }

      // Process completed line
      if (lineEndInBuffer > lineStartInBuffer || foundLineEnd) {
        currentLine++;
        bufferOffset = lineEndInBuffer;

        // Check if we've filled a page
        if (currentLine >= linesPerPage) {
          size_t newPageOffset = absoluteOffset + bufferOffset;
          if (newPageOffset < fileSize && newPageOffset > pageIndex.back()) {
            pageIndex.push_back(newPageOffset);
          }
          currentLine = 0;
        }
      } else {
        // No progress - avoid infinite loop
        bufferOffset++;
      }
    }

    // If we processed the entire buffer, advance absoluteOffset
    if (bufferOffset >= bytesRead) {
      absoluteOffset += bytesRead;
    }
  }

  free(buffer);

  Serial.printf("[%lu] [TXR] Built page index: %zu pages\n", millis(), pageIndex.size());
  return !pageIndex.empty();
}

bool TxtReaderActivity::savePageIndex() const {
  FsFile f;
  if (!SdMan.openFileForWrite("TXR", txt->getCachePath() + "/index.bin", f)) {
    return false;
  }

  // Write header
  uint8_t header[12];
  header[0] = cachedFileSize & 0xFF;
  header[1] = (cachedFileSize >> 8) & 0xFF;
  header[2] = (cachedFileSize >> 16) & 0xFF;
  header[3] = (cachedFileSize >> 24) & 0xFF;
  header[4] = cachedViewportWidth & 0xFF;
  header[5] = (cachedViewportWidth >> 8) & 0xFF;
  header[6] = (cachedViewportWidth >> 16) & 0xFF;
  header[7] = (cachedViewportWidth >> 24) & 0xFF;
  header[8] = cachedLinesPerPage & 0xFF;
  header[9] = (cachedLinesPerPage >> 8) & 0xFF;
  header[10] = (cachedLinesPerPage >> 16) & 0xFF;
  header[11] = (cachedLinesPerPage >> 24) & 0xFF;
  f.write(header, 12);

  // Write page count
  uint32_t pageCount = pageIndex.size();
  uint8_t countBuf[4];
  countBuf[0] = pageCount & 0xFF;
  countBuf[1] = (pageCount >> 8) & 0xFF;
  countBuf[2] = (pageCount >> 16) & 0xFF;
  countBuf[3] = (pageCount >> 24) & 0xFF;
  f.write(countBuf, 4);

  // Write page offsets
  for (size_t offset : pageIndex) {
    uint8_t offsetBuf[4];
    offsetBuf[0] = offset & 0xFF;
    offsetBuf[1] = (offset >> 8) & 0xFF;
    offsetBuf[2] = (offset >> 16) & 0xFF;
    offsetBuf[3] = (offset >> 24) & 0xFF;
    f.write(offsetBuf, 4);
  }

  f.close();
  Serial.printf("[%lu] [TXR] Saved page index\n", millis());
  return true;
}

bool TxtReaderActivity::validatePageIndexCache() const {
  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = renderer.getLineHeight(fontId);

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginLeft += horizontalPadding;
  orientedMarginRight += horizontalPadding;
  orientedMarginTop += verticalPadding;
  orientedMarginBottom += statusBarMargin;

  const int viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  const int linesPerPage = viewportHeight / lineHeight;

  // Validate against current settings
  if (cachedFileSize != txt->getFileSize()) {
    Serial.printf("[%lu] [TXR] Cache invalid: file size changed\n", millis());
    return false;
  }
  if (cachedViewportWidth != viewportWidth) {
    Serial.printf("[%lu] [TXR] Cache invalid: viewport width changed\n", millis());
    return false;
  }
  if (cachedLinesPerPage != linesPerPage) {
    Serial.printf("[%lu] [TXR] Cache invalid: lines per page changed\n", millis());
    return false;
  }

  return true;
}

size_t TxtReaderActivity::getNextUtf8Char(const uint8_t* text, size_t offset, size_t maxLen) const {
  if (offset >= maxLen) return offset;

  uint8_t byte = text[offset];
  size_t charLen = 1;

  if ((byte & 0x80) == 0) {
    // ASCII
    charLen = 1;
  } else if ((byte & 0xE0) == 0xC0) {
    // 2-byte UTF-8
    charLen = 2;
  } else if ((byte & 0xF0) == 0xE0) {
    // 3-byte UTF-8
    charLen = 3;
  } else if ((byte & 0xF8) == 0xF0) {
    // 4-byte UTF-8
    charLen = 4;
  }

  // Ensure we don't exceed buffer
  if (offset + charLen > maxLen) {
    charLen = maxLen - offset;
  }

  return offset + charLen;
}
