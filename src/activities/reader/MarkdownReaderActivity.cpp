/**
 * MarkdownReaderActivity.cpp
 *
 * Markdown reader activity implementation
 */

#include "MarkdownReaderActivity.h"

#include <Epub/Page.h>
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
constexpr unsigned long skipPageMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr int horizontalPadding = 5;
constexpr int statusBarMargin = 19;
}  // namespace

void MarkdownReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<MarkdownReaderActivity*>(param);
  self->displayTaskLoop();
}

void MarkdownReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!markdown) {
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

  markdown->setupCacheDir();

  // Load saved progress
  FsFile f;
  if (SdMan.openFileForRead("MDR", markdown->getCachePath() + "/progress.bin", f)) {
    uint8_t data[2];
    if (f.read(data, 2) == 2) {
      nextPageNumber = data[0] + (data[1] << 8);
      Serial.printf("[%lu] [MDR] Loaded cache: page %d\n", millis(), nextPageNumber);
    }
    f.close();
  }

  // Save current markdown as last opened book
  APP_STATE.openEpubPath = markdown->getPath();
  APP_STATE.saveToFile();

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&MarkdownReaderActivity::taskTrampoline, "MarkdownReaderActivityTask",
              8192,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void MarkdownReaderActivity::onExit() {
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
  markdown.reset();
}

void MarkdownReaderActivity::loop() {
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

  // No current section, attempt to rerender the document
  if (!section) {
    updateRequired = true;
    return;
  }

  // Handle end of document (any button goes back to last page)
  if (section->currentPage >= section->pageCount) {
    section->currentPage = section->pageCount > 0 ? section->pageCount - 1 : 0;
    updateRequired = true;
    return;
  }

  const bool skipPages = mappedInput.getHeldTime() > skipPageMs;
  const int skipAmount = skipPages ? 10 : 1;

  if (prevReleased) {
    if (section->currentPage >= skipAmount) {
      section->currentPage -= skipAmount;
    } else {
      section->currentPage = 0;
    }
    updateRequired = true;
  } else {
    section->currentPage += skipAmount;
    if (section->currentPage >= section->pageCount) {
      section->currentPage = section->pageCount;  // Allow showing "End of book"
    }
    updateRequired = true;
  }
}

void MarkdownReaderActivity::displayTaskLoop() {
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

void MarkdownReaderActivity::renderScreen() {
  // Double-check subactivity under mutex protection to prevent race condition
  if (subActivity) {
    return;
  }

  if (!markdown) {
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
    Serial.printf("[%lu] [MDR] Loading markdown section\n", millis());
    section = std::unique_ptr<MarkdownSection>(new MarkdownSection(markdown, renderer));

    const auto viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const auto viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
    const auto config = SETTINGS.getRenderConfig(viewportWidth, viewportHeight);

    if (!section->loadMarkdownSectionFile(config)) {
      Serial.printf("[%lu] [MDR] Cache not found, building...\n", millis());

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

      if (!section->createMarkdownSectionFile(config)) {
        Serial.printf("[%lu] [MDR] Failed to persist page data to SD\n", millis());
        section.reset();
        // Show error message to user
        renderer.clearScreen(THEME.backgroundColor);
        renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "Failed to load markdown", THEME.primaryTextBlack,
                                  BOLD);
        renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
        renderer.displayBuffer();
        return;
      }
    } else {
      Serial.printf("[%lu] [MDR] Cache found, skipping build...\n", millis());
    }

    if (nextPageNumber >= section->pageCount) {
      section->currentPage = section->pageCount > 0 ? section->pageCount - 1 : 0;
    } else {
      section->currentPage = nextPageNumber;
    }
  }

  // Show end of document screen
  if (section->currentPage >= section->pageCount) {
    renderer.clearScreen(THEME.backgroundColor);
    renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "End of document", THEME.primaryTextBlack, BOLD);
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen(THEME.backgroundColor);

  if (section->pageCount == 0) {
    Serial.printf("[%lu] [MDR] No pages to render\n", millis());
    renderTitlePage(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    Serial.printf("[%lu] [MDR] Page out of bounds: %d (max %d)\n", millis(), section->currentPage, section->pageCount);
    renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "Out of bounds", THEME.primaryTextBlack, BOLD);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  {
    auto p = section->loadPageFromMarkdownSectionFile();
    if (!p) {
      Serial.printf("[%lu] [MDR] Failed to load page from SD - clearing section cache\n", millis());
      section->clearCache();
      section.reset();
      if (renderRetryCount++ < 3) {
        return renderScreen();
      }
      Serial.printf("[%lu] [MDR] Retry limit reached, giving up\n", millis());
      renderer.drawCenteredText(SETTINGS.getReaderFontId(), 300, "Failed to load page", THEME.primaryTextBlack, BOLD);
      renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
      renderer.displayBuffer();
      return;
    }
    renderRetryCount = 0;

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    Serial.printf("[%lu] [MDR] Rendered page in %dms\n", millis(), millis() - start);
  }

  // Save progress
  FsFile f;
  if (SdMan.openFileForWrite("MDR", markdown->getCachePath() + "/progress.bin", f)) {
    uint8_t data[2];
    data[0] = section->currentPage & 0xFF;
    data[1] = (section->currentPage >> 8) & 0xFF;
    f.write(data, 2);
    f.close();
  }
}

void MarkdownReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
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

void MarkdownReaderActivity::renderTitlePage(const int orientedMarginTop, const int orientedMarginRight,
                                             const int orientedMarginBottom, const int orientedMarginLeft) {
  (void)orientedMarginTop;
  (void)orientedMarginLeft;
  const int fontId = SETTINGS.getReaderFontId();
  const int screenHeight = renderer.getScreenHeight();

  const std::string& title = markdown->getTitle();
  if (!title.empty()) {
    renderer.drawCenteredText(fontId, screenHeight / 3, title.c_str(), THEME.primaryTextBlack, BOLD);
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

void MarkdownReaderActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                             const int orientedMarginLeft) const {
  // Determine visible status bar elements
  const bool showProgress = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;
  const bool showBattery = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                           SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;
  const bool showTitle = SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::NO_PROGRESS ||
                         SETTINGS.statusBar == CrossPointSettings::STATUS_BAR_MODE::FULL;

  // Position status bar near the bottom of the logical screen, regardless of orientation
  const auto screenHeight = renderer.getScreenHeight();
  const auto textY = screenHeight - orientedMarginBottom + 2;
  int percentageTextWidth = 0;
  int progressTextWidth = 0;

  if (showProgress && section) {
    // Calculate progress in document
    const uint8_t bookProgress =
        section->pageCount > 0 ? static_cast<uint8_t>((section->currentPage + 1) * 100 / section->pageCount) : 0;

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

    // Battery icon
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

  if (showTitle && markdown) {
    // Centered title text
    const int batteryAreaWidth = showBattery ? 20 + percentageTextWidth : 0;
    const int titleMarginLeft = batteryAreaWidth + 30 + orientedMarginLeft;
    const int titleMarginRight = progressTextWidth + 30 + orientedMarginRight;
    const int availableTextWidth = renderer.getScreenWidth() - titleMarginLeft - titleMarginRight;

    std::string title = markdown->getTitle();
    int titleWidth = renderer.getTextWidth(THEME.smallFontId, title.c_str());
    while (titleWidth > availableTextWidth && title.length() > 11) {
      title.replace(title.length() - 8, 8, "...");
      titleWidth = renderer.getTextWidth(THEME.smallFontId, title.c_str());
    }

    renderer.drawText(THEME.smallFontId, titleMarginLeft + (availableTextWidth - titleWidth) / 2, textY, title.c_str(),
                      THEME.primaryTextBlack);
  }
}
