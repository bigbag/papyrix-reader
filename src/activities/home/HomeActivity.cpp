#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ScreenComponents.h"
#include "ThemeManager.h"
#include "config.h"
#include "util/StringUtils.h"

void HomeActivity::taskTrampoline(void* param) {
  auto* self = static_cast<HomeActivity*>(param);
  self->displayTaskLoop();
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  // Check if we have a book to continue reading
  hasContinueReading = !APP_STATE.openEpubPath.empty() && SdMan.exists(APP_STATE.openEpubPath.c_str());

  // Load book metadata if enabled and we have a book to continue
  lastBookTitle.clear();
  lastBookAuthor.clear();
  if (hasContinueReading) {
    // Extract filename as fallback
    lastBookTitle = APP_STATE.openEpubPath;
    const size_t lastSlash = lastBookTitle.find_last_of('/');
    if (lastSlash != std::string::npos) {
      lastBookTitle = lastBookTitle.substr(lastSlash + 1);
    }

    // Check file extension and try to load metadata
    if (StringUtils::isEpubFile(lastBookTitle)) {
      // Always try to load EPUB metadata for home screen display
      Epub epub(APP_STATE.openEpubPath, PAPYRIX_DIR);
      if (epub.load(false)) {
        if (!epub.getTitle().empty()) {
          lastBookTitle = std::string(epub.getTitle());
        }
        if (!epub.getAuthor().empty()) {
          lastBookAuthor = std::string(epub.getAuthor());
        }
        // Try to generate thumbnail image for Continue Reading card
        if (epub.generateThumbBmp()) {
          coverBmpPath = epub.getThumbBmpPath();
          hasCoverImage = true;
        }
      }
    } else if (StringUtils::isXtcFile(lastBookTitle) || StringUtils::isTxtFile(lastBookTitle)) {
      // Strip known extensions from non-EPUB files
      const size_t dotPos = lastBookTitle.find_last_of('.');
      if (dotPos != std::string::npos) {
        lastBookTitle.resize(dotPos);
      }
    }
  }

  // Start at book card (0) if continue available, otherwise Files (1)
  selectorIndex = hasContinueReading ? 0 : 1;

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&HomeActivity::taskTrampoline, "HomeActivityTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const bool prevPressed = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool nextPressed = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Right);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // 0 = Book card (continue reading), 1 = Files, 2 = Settings
    if (selectorIndex == 0 && hasContinueReading) {
      onContinueReading();
    } else if (selectorIndex == 1) {
      onReaderOpen();
    } else if (selectorIndex == 2) {
      onSettingsOpen();
    }
  } else if (prevPressed) {
    int newIndex = selectorIndex - 1;
    // Skip book card if no book to continue
    if (newIndex == 0 && !hasContinueReading) {
      newIndex = 2;  // Wrap to Settings
    }
    if (newIndex < 0) {
      newIndex = 2;
    }
    selectorIndex = newIndex;
    updateRequired = true;
  } else if (nextPressed) {
    int newIndex = selectorIndex + 1;
    if (newIndex > 2) {
      newIndex = hasContinueReading ? 0 : 1;
    }
    // Skip book card if no book to continue
    if (newIndex == 0 && !hasContinueReading) {
      newIndex = 1;
    }
    selectorIndex = newIndex;
    updateRequired = true;
  }
}

void HomeActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void HomeActivity::render() {
  // If we have a stored cover buffer, restore it instead of clearing
  const bool bufferRestored = coverBufferStored && restoreCoverBuffer();
  if (!bufferRestored) {
    renderer.clearScreen(THEME.backgroundColor);
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Draw title "Papyrix Reader" at top
  renderer.drawCenteredText(THEME.readerFontId, 10, "Papyrix Reader", THEME.primaryTextBlack, BOLD);

  // Battery indicator - top right
  const int batteryX = pageWidth - 60;
  const int batteryY = 10;
  ScreenComponents::drawBattery(renderer, batteryX, batteryY);

  // Book card constants - larger ratio for more prominent display
  const int cardWidth = pageWidth * 3 / 5;     // 288px on 480px screen (60%)
  const int cardHeight = pageHeight / 2 + 50;  // 450px on 800px screen
  const int cardX = (pageWidth - cardWidth) / 2;
  constexpr int cardY = 50;  // Below "Papyrix Reader" title

  // Book card selection state
  const bool cardSelected = (selectorIndex == 0) && hasContinueReading;

  // Bookmark dimensions (used in multiple places)
  const int bookmarkWidth = 30;
  const int bookmarkHeight = 50;
  const int bookmarkX = cardX + cardWidth - bookmarkWidth - 15;  // Right side with padding
  const int bookmarkY = cardY + 15;

  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading && hasCoverImage && !coverBmpPath.empty() && !coverRendered) {
    // First time: load cover from SD and render
    FsFile file;
    if (SdMan.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        // Calculate position to center image within the book card
        int coverX, coverY;

        if (bitmap.getWidth() > cardWidth || bitmap.getHeight() > cardHeight) {
          const float imgRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
          const float boxRatio = static_cast<float>(cardWidth) / static_cast<float>(cardHeight);

          if (imgRatio > boxRatio) {
            coverX = cardX;
            coverY = cardY + (cardHeight - static_cast<int>(cardWidth / imgRatio)) / 2;
          } else {
            coverX = cardX + (cardWidth - static_cast<int>(cardHeight * imgRatio)) / 2;
            coverY = cardY;
          }
        } else {
          coverX = cardX + (cardWidth - bitmap.getWidth()) / 2;
          coverY = cardY + (cardHeight - bitmap.getHeight()) / 2;
        }

        // Draw the cover image centered within the book card
        renderer.drawBitmap(bitmap, coverX, coverY, cardWidth, cardHeight);

        // Draw border around the card
        renderer.drawRect(cardX, cardY, cardWidth, cardHeight, THEME.primaryTextBlack);

        // Store the buffer with cover image for fast navigation
        coverBufferStored = storeCoverBuffer();
        coverRendered = true;

        // First render: if selected, draw selection indicators now
        if (cardSelected) {
          renderer.drawRect(cardX + 1, cardY + 1, cardWidth - 2, cardHeight - 2, THEME.primaryTextBlack);
          renderer.drawRect(cardX + 2, cardY + 2, cardWidth - 4, cardHeight - 4, THEME.primaryTextBlack);
        }
      }
      file.close();
    }
  } else if (!bufferRestored && !coverRendered) {
    // No cover image: draw border or fill, plus bookmark as visual flair
    if (cardSelected) {
      renderer.fillRect(cardX, cardY, cardWidth, cardHeight, THEME.primaryTextBlack);
    } else {
      renderer.drawRect(cardX, cardY, cardWidth, cardHeight, THEME.primaryTextBlack);
    }

    // Draw bookmark ribbon when no cover image (visual decoration)
    if (hasContinueReading) {
      // Text and bookmark color inverts based on selection
      const bool bookmarkColor = !cardSelected ? THEME.primaryTextBlack : !THEME.primaryTextBlack;
      // Bookmark shape: rectangle with triangular notch at bottom
      renderer.fillRect(bookmarkX, bookmarkY, bookmarkWidth, bookmarkHeight - 10, bookmarkColor);
      // Draw triangular notch using two small rectangles to simulate
      renderer.fillRect(bookmarkX, bookmarkY + bookmarkHeight - 10, bookmarkWidth / 2 - 2, 10, bookmarkColor);
      renderer.fillRect(bookmarkX + bookmarkWidth / 2 + 2, bookmarkY + bookmarkHeight - 10, bookmarkWidth / 2 - 2, 10,
                        bookmarkColor);
    }
  }

  // If buffer was restored, draw selection indicators if needed
  if (bufferRestored && cardSelected && coverRendered) {
    // Draw selection border
    renderer.drawRect(cardX + 1, cardY + 1, cardWidth - 2, cardHeight - 2, THEME.primaryTextBlack);
    renderer.drawRect(cardX + 2, cardY + 2, cardWidth - 4, cardHeight - 4, THEME.primaryTextBlack);
  }

  // Text and bookmark color inverts based on selection
  const bool cardTextColor = !cardSelected ? THEME.primaryTextBlack : !THEME.primaryTextBlack;
  // Card text color (used for text when no cover image)
  const bool textOnCard = coverRendered ? THEME.primaryTextBlack : cardTextColor;

  if (hasContinueReading) {
    // Word wrap title into lines (max 3 lines)
    const int maxLineWidth = cardWidth - 40;
    const int titleLineHeight = renderer.getLineHeight(THEME.uiFontId);
    const int spaceWidth = renderer.getSpaceWidth(THEME.uiFontId);

    // Split into words
    std::vector<std::string> words;
    words.reserve(8);
    size_t pos = 0;
    while (pos < lastBookTitle.size()) {
      while (pos < lastBookTitle.size() && lastBookTitle[pos] == ' ') ++pos;
      if (pos >= lastBookTitle.size()) break;
      const size_t start = pos;
      while (pos < lastBookTitle.size() && lastBookTitle[pos] != ' ') ++pos;
      words.emplace_back(lastBookTitle.substr(start, pos - start));
    }

    // Build lines with word wrapping
    std::vector<std::string> lines;
    std::string currentLine;

    for (auto& word : words) {
      if (lines.size() >= 3) {
        // At line limit, add ellipsis to last line
        lines.back().append("...");
        while (!lines.back().empty() && renderer.getTextWidth(THEME.uiFontId, lines.back().c_str()) > maxLineWidth) {
          lines.back().resize(lines.back().size() - 5);
          lines.back().append("...");
        }
        break;
      }

      // Truncate word if too long
      int wordWidth = renderer.getTextWidth(THEME.uiFontId, word.c_str());
      while (wordWidth > maxLineWidth && word.size() > 5) {
        word.resize(word.size() - 5);
        word.append("...");
        wordWidth = renderer.getTextWidth(THEME.uiFontId, word.c_str());
      }

      int newLineWidth = renderer.getTextWidth(THEME.uiFontId, currentLine.c_str());
      if (newLineWidth > 0) newLineWidth += spaceWidth;
      newLineWidth += wordWidth;

      if (newLineWidth > maxLineWidth && !currentLine.empty()) {
        lines.push_back(currentLine);
        currentLine = word;
      } else {
        if (!currentLine.empty()) currentLine.append(" ");
        currentLine.append(word);
      }
    }

    if (!currentLine.empty() && lines.size() < 3) {
      lines.push_back(currentLine);
    }

    // Calculate total text block height for vertical centering
    int totalTextHeight = titleLineHeight * static_cast<int>(lines.size());
    if (!lastBookAuthor.empty()) {
      totalTextHeight += titleLineHeight * 3 / 2;  // Author line with spacing
    }

    // Vertically center within card (leaving space for bookmark at top and "Continue Reading" at bottom)
    const int textAreaTop = cardY + 70;                  // Below bookmark
    const int textAreaBottom = cardY + cardHeight - 50;  // Above "Continue Reading"
    int titleY = textAreaTop + (textAreaBottom - textAreaTop - totalTextHeight) / 2;

    // If cover image was rendered, draw white box behind title and author
    if (coverRendered) {
      constexpr int boxPadding = 8;
      // Calculate the max text width for the box
      int maxTextWidth = 0;
      for (const auto& line : lines) {
        const int lineWidth = renderer.getTextWidth(THEME.uiFontId, line.c_str());
        if (lineWidth > maxTextWidth) {
          maxTextWidth = lineWidth;
        }
      }
      if (!lastBookAuthor.empty()) {
        std::string trimmedAuthor = lastBookAuthor;
        while (renderer.getTextWidth(THEME.uiFontId, trimmedAuthor.c_str()) > maxLineWidth && trimmedAuthor.size() > 5) {
          trimmedAuthor.resize(trimmedAuthor.size() - 5);
          trimmedAuthor.append("...");
        }
        const int authorWidth = renderer.getTextWidth(THEME.uiFontId, trimmedAuthor.c_str());
        if (authorWidth > maxTextWidth) {
          maxTextWidth = authorWidth;
        }
      }

      const int boxWidth = maxTextWidth + boxPadding * 2;
      const int boxHeight = totalTextHeight + boxPadding * 2;
      const int boxX = (pageWidth - boxWidth) / 2;
      const int boxY = titleY - boxPadding;

      // Draw white filled box
      renderer.fillRect(boxX, boxY, boxWidth, boxHeight, !THEME.primaryTextBlack);
      // Draw black border around the box
      renderer.drawRect(boxX, boxY, boxWidth, boxHeight, THEME.primaryTextBlack);
    }

    // Draw title lines centered
    for (const auto& line : lines) {
      const int lineWidth = renderer.getTextWidth(THEME.uiFontId, line.c_str());
      const int lineX = cardX + (cardWidth - lineWidth) / 2;
      renderer.drawText(THEME.uiFontId, lineX, titleY, line.c_str(), textOnCard);
      titleY += titleLineHeight;
    }

    // Show author if available
    if (!lastBookAuthor.empty()) {
      titleY += titleLineHeight / 2;  // Extra spacing before author
      std::string trimmedAuthor = lastBookAuthor;
      while (renderer.getTextWidth(THEME.uiFontId, trimmedAuthor.c_str()) > maxLineWidth && trimmedAuthor.size() > 5) {
        trimmedAuthor.resize(trimmedAuthor.size() - 5);
        trimmedAuthor.append("...");
      }
      const int authorWidth = renderer.getTextWidth(THEME.uiFontId, trimmedAuthor.c_str());
      const int authorX = cardX + (cardWidth - authorWidth) / 2;
      renderer.drawText(THEME.uiFontId, authorX, titleY, trimmedAuthor.c_str(), textOnCard);
    }

    // "Continue Reading" at bottom of card
    const char* continueText = "Continue Reading";
    const int continueWidth = renderer.getTextWidth(THEME.uiFontId, continueText);
    const int continueX = cardX + (cardWidth - continueWidth) / 2;
    const int continueY = cardY + cardHeight - 40;

    if (coverRendered) {
      // Draw white box behind "Continue Reading" text
      constexpr int continuePadding = 6;
      const int continueBoxWidth = continueWidth + continuePadding * 2;
      const int continueBoxHeight = titleLineHeight + continuePadding;
      const int continueBoxX = (pageWidth - continueBoxWidth) / 2;
      const int continueBoxY = continueY - continuePadding / 2;
      renderer.fillRect(continueBoxX, continueBoxY, continueBoxWidth, continueBoxHeight, !THEME.primaryTextBlack);
      renderer.drawRect(continueBoxX, continueBoxY, continueBoxWidth, continueBoxHeight, THEME.primaryTextBlack);
      renderer.drawText(THEME.uiFontId, continueX, continueY, continueText, THEME.primaryTextBlack);
    } else {
      renderer.drawText(THEME.uiFontId, continueX, continueY, continueText, textOnCard);
    }
  } else {
    // No book open - show placeholder
    const char* noBookText = "No book open";
    const int noBookWidth = renderer.getTextWidth(THEME.uiFontId, noBookText);
    const int noBookX = cardX + (cardWidth - noBookWidth) / 2;
    const int noBookY = cardY + cardHeight / 2 - renderer.getFontAscenderSize(THEME.uiFontId) / 2;
    renderer.drawText(THEME.uiFontId, noBookX, noBookY, noBookText, textOnCard);
  }

  // Grid 2x1 at bottom of page (Files, Setup) - aligned with button hints
  // Button hints use: positions {25, 130, 245, 350} with width 106 each
  constexpr int gridItemHeight = 50;
  constexpr int buttonHintsY = 50;                                    // Distance from bottom for button hints
  const int gridY = pageHeight - buttonHintsY - gridItemHeight - 10;  // 10px above buttons

  // Grid positions matching button hint pairs
  constexpr int gridPositions[] = {25, 245};  // Left aligns with btn1, Right aligns with btn3
  constexpr int gridItemWidth = 211;          // Spans 2 button widths + gap (106 + 106 - 1)

  // Menu items in 2x1 grid
  const char* menuItems[] = {"Files", "Settings"};

  for (int i = 0; i < 2; i++) {
    const int itemX = gridPositions[i];
    const bool isSelected = (selectorIndex == i + 1);  // +1 because 0 is book card

    if (isSelected) {
      // Selected - filled background
      renderer.fillRect(itemX, gridY, gridItemWidth, gridItemHeight, THEME.selectionFillBlack);
    } else {
      // Not selected - bordered
      renderer.drawRect(itemX, gridY, gridItemWidth, gridItemHeight, THEME.primaryTextBlack);
    }

    // Draw centered text
    const bool itemTextColor = isSelected ? THEME.selectionTextBlack : THEME.primaryTextBlack;
    const int textWidth = renderer.getTextWidth(THEME.uiFontId, menuItems[i]);
    const int textX = itemX + (gridItemWidth - textWidth) / 2;
    const int textY = gridY + (gridItemHeight - renderer.getFontAscenderSize(THEME.uiFontId)) / 2;
    renderer.drawText(THEME.uiFontId, textX, textY, menuItems[i], itemTextColor);
  }

  // Button hints at bottom
  const auto btnLabels = mappedInput.mapLabels("Back", "Confirm", "Left", "Right");
  renderer.drawButtonHints(THEME.uiFontId, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4,
                           THEME.primaryTextBlack);

  renderer.displayBuffer();
}
