#include "EpubReaderActivity.h"

#include <Bitmap.h>
#include <Epub/Page.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "config.h"

namespace {
constexpr unsigned long skipChapterMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr float lineCompression = 0.95f;
constexpr int horizontalPadding = 5;
constexpr int statusBarMargin = 19;
}  // namespace

void EpubReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<EpubReaderActivity*>(param);
  self->displayTaskLoop();
}

void EpubReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!epub) {
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

  epub->setupCacheDir();

  FsFile f;
  if (SdMan.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      Serial.printf("[%lu] [ERS] Loaded cache: %d, %d\n", millis(), currentSpineIndex, nextPageNumber);
    }
    f.close();
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      Serial.printf("[%lu] [ERS] Opened for first time, navigating to text reference at index %d\n", millis(),
                    textSpineIndex);
    }
  }

  // Save current epub as last opened epub
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&EpubReaderActivity::taskTrampoline, "EpubReaderActivityTask",
              8192,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void EpubReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  section.reset();
  epub.reset();
}

void EpubReaderActivity::loop() {
  // Pass input responsibility to sub activity if exists
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Enter chapter selection activity
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Don't start activity transition while rendering
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    exitActivity();
    enterNewActivity(new EpubReaderChapterSelectionActivity(
        this->renderer, this->mappedInput, epub, currentSpineIndex,
        [this] {
          exitActivity();
          updateRequired = true;
        },
        [this](const int newSpineIndex) {
          if (currentSpineIndex != newSpineIndex) {
            currentSpineIndex = newSpineIndex;
            nextPageNumber = 0;
            section.reset();
          }
          exitActivity();
          updateRequired = true;
        }));
    xSemaphoreGive(renderingMutex);
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

  // any button press when at end of the book goes back to the last page
  if (currentSpineIndex >= epub->getSpineItemsCount() && epub->getSpineItemsCount() > 0) {
    currentSpineIndex = epub->getSpineItemsCount() - 1;
    nextPageNumber = UINT16_MAX;
    updateRequired = true;
    return;
  }

  const bool skipChapter = mappedInput.getHeldTime() > skipChapterMs;

  if (skipChapter) {
    // We don't want to delete the section mid-render, so grab the semaphore
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    nextPageNumber = 0;
    currentSpineIndex = nextReleased ? currentSpineIndex + 1 : currentSpineIndex - 1;
    section.reset();
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    updateRequired = true;
    return;
  }

  if (prevReleased) {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      nextPageNumber = UINT16_MAX;
      currentSpineIndex--;
      section.reset();
      xSemaphoreGive(renderingMutex);
    }
    updateRequired = true;
  } else {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      nextPageNumber = 0;
      currentSpineIndex++;
      section.reset();
      xSemaphoreGive(renderingMutex);
    }
    updateRequired = true;
  }
}

void EpubReaderActivity::displayTaskLoop() {
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

void EpubReaderActivity::renderScreen() {
  // Double-check subactivity under mutex protection to prevent race condition
  if (subActivity) {
    return;
  }

  if (!epub) {
    return;
  }

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    renderer.clearScreen(THEME.backgroundColor);
    renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "End of book", THEME.primaryTextBlack, BOLD);
    renderer.displayBuffer();
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginLeft += horizontalPadding;
  orientedMarginRight += horizontalPadding;
  orientedMarginBottom += statusBarMargin;

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    Serial.printf("[%lu] [ERS] Loading file: %s, index: %d\n", millis(), filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));

    const auto viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const auto viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

    if (!section->loadSectionFile(SETTINGS.getReaderFontId(), lineCompression, SETTINGS.extraParagraphSpacing,
                                  SETTINGS.paragraphAlignment, SETTINGS.hyphenation, viewportWidth, viewportHeight)) {
      Serial.printf("[%lu] [ERS] Cache not found, building...\n", millis());

      // Show static "Indexing..." message
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

      if (!section->createSectionFile(SETTINGS.getReaderFontId(), lineCompression, SETTINGS.extraParagraphSpacing,
                                      SETTINGS.paragraphAlignment, SETTINGS.hyphenation, viewportWidth, viewportHeight,
                                      nullptr, nullptr)) {
        Serial.printf("[%lu] [ERS] Failed to persist page data to SD\n", millis());
        section.reset();
        // Show error message to user
        renderer.clearScreen(THEME.backgroundColor);
        renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "Failed to load chapter", THEME.primaryTextBlack,
                                  BOLD);
        renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
        renderer.displayBuffer();
        return;
      }
    } else {
      Serial.printf("[%lu] [ERS] Cache found, skipping build...\n", millis());
    }

    if (nextPageNumber == UINT16_MAX) {
      section->currentPage = section->pageCount - 1;
    } else {
      section->currentPage = nextPageNumber;
    }
  }

  renderer.clearScreen(THEME.backgroundColor);

  if (section->pageCount == 0) {
    Serial.printf("[%lu] [ERS] No pages to render\n", millis());

    // Render cover image if this is spine[0] and cover exists
    if (currentSpineIndex == 0) {
      if (epub->generateCoverBmp()) {
        Serial.printf("[%lu] [ERS] Rendering cover page from BMP\n", millis());
        renderCoverPage(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
      } else {
        Serial.printf("[%lu] [ERS] Cover failed, rendering title page\n", millis());
        renderTitlePage(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
      }
      return;
    }

    renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "Empty chapter", THEME.primaryTextBlack, BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    Serial.printf("[%lu] [ERS] Page out of bounds: %d (max %d)\n", millis(), section->currentPage, section->pageCount);
    renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "Out of bounds", THEME.primaryTextBlack, BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  {
    auto p = section->loadPageFromSectionFile();
    if (!p) {
      Serial.printf("[%lu] [ERS] Failed to load page from SD - clearing section cache\n", millis());
      section->clearCache();
      section.reset();
      return renderScreen();
    }

    // Check if this is an empty cover page (spine[0], page 0, no elements)
    if (currentSpineIndex == 0 && section->currentPage == 0 && p->elements.empty()) {
      if (epub->generateCoverBmp()) {
        Serial.printf("[%lu] [ERS] Empty cover page detected, rendering cover BMP\n", millis());
        renderCoverPage(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
      } else {
        Serial.printf("[%lu] [ERS] Cover failed, rendering title page\n", millis());
        renderTitlePage(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
      }
    } else {
      const auto start = millis();
      renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
      Serial.printf("[%lu] [ERS] Rendered page in %dms\n", millis(), millis() - start);
    }
  }

  FsFile f;
  if (SdMan.openFileForWrite("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentSpineIndex & 0xFF;
    data[1] = (currentSpineIndex >> 8) & 0xFF;
    data[2] = section->currentPage & 0xFF;
    data[3] = (section->currentPage >> 8) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const int fontId = SETTINGS.getReaderFontId();
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, THEME.primaryTextBlack);
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(EInkDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getPagesPerRefreshValue();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  // Grayscale text rendering (anti-aliasing)
  if (SETTINGS.textAntiAliasing && renderer.fontSupportsGrayscale(fontId)) {
    // Save bw buffer to reset buffer state after grayscale data sync
    renderer.storeBwBuffer();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, THEME.primaryTextBlack);
    renderer.copyGrayscaleLsbBuffers();

    // Render and copy to MSB buffer
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, THEME.primaryTextBlack);
    renderer.copyGrayscaleMsbBuffers();

    // display grayscale part
    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);

    // restore the bw data
    renderer.restoreBwBuffer();
  }
}

void EpubReaderActivity::renderCoverPage(const int orientedMarginTop, const int orientedMarginRight,
                                         const int orientedMarginBottom, const int orientedMarginLeft) {
  FsFile coverFile;
  if (!SdMan.openFileForRead("ERS", epub->getCoverBmpPath(), coverFile)) {
    Serial.printf("[%lu] [ERS] Failed to open cover BMP\n", millis());
    renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "Cover unavailable", THEME.primaryTextBlack, BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  Bitmap bitmap(coverFile);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    coverFile.close();
    renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "Cover unavailable", THEME.primaryTextBlack, BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  // Calculate viewport (accounting for margins)
  const int viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

  // Center image in viewport (scaling logic from SleepActivity)
  int x, y;
  if (bitmap.getWidth() > viewportWidth || bitmap.getHeight() > viewportHeight) {
    const float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float viewportRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    if (ratio > viewportRatio) {
      x = orientedMarginLeft;
      y = orientedMarginTop + (viewportHeight - viewportWidth / ratio) / 2;
    } else {
      x = orientedMarginLeft + (viewportWidth - viewportHeight * ratio) / 2;
      y = orientedMarginTop;
    }
  } else {
    x = orientedMarginLeft + (viewportWidth - bitmap.getWidth()) / 2;
    y = orientedMarginTop + (viewportHeight - bitmap.getHeight()) / 2;
  }

  renderer.drawBitmap(bitmap, x, y, viewportWidth, viewportHeight);
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  // Display with refresh logic
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(EInkDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getPagesPerRefreshValue();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  // Grayscale rendering (if bitmap supports it)
  if (bitmap.hasGreyscale()) {
    renderer.storeBwBuffer();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, viewportWidth, viewportHeight);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, viewportWidth, viewportHeight);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.restoreBwBuffer();
  }

  coverFile.close();
}

void EpubReaderActivity::renderTitlePage(const int orientedMarginTop, const int orientedMarginRight,
                                         const int orientedMarginBottom, const int orientedMarginLeft) {
  (void)orientedMarginTop;
  (void)orientedMarginLeft;
  const int fontId = SETTINGS.getReaderFontId();
  const int screenHeight = renderer.getScreenHeight();

  const std::string& title = epub->getTitle();
  if (!title.empty()) {
    renderer.drawCenteredText(fontId, screenHeight / 3, title.c_str(), THEME.primaryTextBlack, BOLD);
  }

  const std::string& author = epub->getAuthor();
  if (!author.empty()) {
    renderer.drawCenteredText(fontId, screenHeight / 2, author.c_str(), THEME.primaryTextBlack, REGULAR);
  }

  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(EInkDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getPagesPerRefreshValue();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }
}

void EpubReaderActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                         const int orientedMarginLeft) const {
  // determine visible status bar elements
  const bool showProgress = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;
  const bool showBattery = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                           SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;
  const bool showChapterTitle = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                                SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;

  // Position status bar near the bottom of the logical screen, regardless of orientation
  const auto screenHeight = renderer.getScreenHeight();
  const auto textY = screenHeight - orientedMarginBottom + 2;
  int percentageTextWidth = 0;
  int progressTextWidth = 0;

  if (showProgress) {
    // Calculate progress in book (guard against division by zero)
    const float sectionChapterProg =
        section->pageCount > 0 ? static_cast<float>(section->currentPage) / section->pageCount : 0.0f;
    const uint8_t bookProgress = epub->calculateProgress(currentSpineIndex, sectionChapterProg);

    // Right aligned text for progress counter
    char progress[32];
    snprintf(progress, sizeof(progress), "%d/%d  %u%%", section->currentPage + 1, section->pageCount, bookProgress);
    progressTextWidth = renderer.getTextWidth(THEME.smallFontId, progress);
    renderer.drawText(THEME.smallFontId, renderer.getScreenWidth() - orientedMarginRight - progressTextWidth, textY,
                      progress, THEME.primaryTextBlack);
  }

  if (showBattery) {
    // Left aligned battery icon and percentage
    const uint16_t millivolts = battery.readMillivolts();
    char percentageText[8];
    uint16_t percentage = 0;
    if (millivolts < 100) {
      // Invalid reading - show error indicator
      Serial.printf("[BAT] Invalid reading: millivolts=%u, showing --%%\n", millivolts);
      snprintf(percentageText, sizeof(percentageText), "--%%");
    } else {
      percentage = BatteryMonitor::percentageFromMillivolts(millivolts);
      Serial.printf("[BAT] millivolts=%u, percentage=%u%%\n", millivolts, percentage);
      snprintf(percentageText, sizeof(percentageText), "%u%%", percentage);
    }
    percentageTextWidth = renderer.getTextWidth(THEME.smallFontId, percentageText);
    renderer.drawText(THEME.smallFontId, 20 + orientedMarginLeft, textY, percentageText, THEME.primaryTextBlack);

    // 1 column on left, 2 columns on right, 5 columns of battery body
    constexpr int batteryWidth = 15;
    constexpr int batteryHeight = 10;
    const int x = orientedMarginLeft;
    const int y = screenHeight - orientedMarginBottom + 5;

    // Top line
    renderer.drawLine(x, y, x + batteryWidth - 4, y, THEME.primaryTextBlack);
    // Bottom line
    renderer.drawLine(x, y + batteryHeight - 1, x + batteryWidth - 4, y + batteryHeight - 1, THEME.primaryTextBlack);
    // Left line
    renderer.drawLine(x, y, x, y + batteryHeight - 1, THEME.primaryTextBlack);
    // Battery end
    renderer.drawLine(x + batteryWidth - 4, y, x + batteryWidth - 4, y + batteryHeight - 1, THEME.primaryTextBlack);
    renderer.drawLine(x + batteryWidth - 3, y + 2, x + batteryWidth - 1, y + 2, THEME.primaryTextBlack);
    renderer.drawLine(x + batteryWidth - 3, y + batteryHeight - 3, x + batteryWidth - 1, y + batteryHeight - 3,
                      THEME.primaryTextBlack);
    renderer.drawLine(x + batteryWidth - 1, y + 2, x + batteryWidth - 1, y + batteryHeight - 3, THEME.primaryTextBlack);

    // The +1 is to round up, so that we always fill at least one pixel
    int filledWidth = percentage * (batteryWidth - 5) / 100 + 1;
    if (filledWidth > batteryWidth - 5) {
      filledWidth = batteryWidth - 5;  // Ensure we don't overflow
    }
    renderer.fillRect(x + 1, y + 1, filledWidth, batteryHeight - 2, THEME.primaryTextBlack);
  }

  if (showChapterTitle) {
    // Centered chatper title text
    // Page width minus existing content with 30px padding on each side
    const int batteryAreaWidth = showBattery ? 20 + percentageTextWidth : 0;
    const int titleMarginLeft = batteryAreaWidth + 30 + orientedMarginLeft;
    const int titleMarginRight = progressTextWidth + 30 + orientedMarginRight;
    const int availableTextWidth = renderer.getScreenWidth() - titleMarginLeft - titleMarginRight;
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);

    std::string title;
    int titleWidth;
    if (tocIndex == -1) {
      title = "Unnamed";
      titleWidth = renderer.getTextWidth(THEME.smallFontId, "Unnamed");
    } else {
      const auto tocItem = epub->getTocItem(tocIndex);
      title = tocItem.title;
      titleWidth = renderer.getTextWidth(THEME.smallFontId, title.c_str());
      while (titleWidth > availableTextWidth && title.length() > 11) {
        title.replace(title.length() - 8, 8, "...");
        titleWidth = renderer.getTextWidth(THEME.smallFontId, title.c_str());
      }
    }

    renderer.drawText(THEME.smallFontId, titleMarginLeft + (availableTextWidth - titleWidth) / 2, textY, title.c_str(),
                      THEME.primaryTextBlack);
  }
}
