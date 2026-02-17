#include "HomeState.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <CoverHelpers.h>
#include <GfxRenderer.h>
#include <Group5.h>
#include <SDCardManager.h>
#include <esp_system.h>

#include "../config.h"
#include "../core/BootMode.h"
#include "../core/Core.h"
#include "Battery.h"
#include "FontManager.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"

namespace papyrix {

HomeState::HomeState(GfxRenderer& renderer) : renderer_(renderer) {}

HomeState::~HomeState() { freeCoverThumbnail(); }

void HomeState::enter(Core& core) {
  Serial.println("[HOME] Entering");

  // Load last book info if content is still open
  loadLastBook(core);

  // Update battery
  updateBattery();

  view_.needsRender = true;
}

void HomeState::exit(Core& core) {
  Serial.println("[HOME] Exiting");
  freeCoverThumbnail();
  view_.clear();
}

void HomeState::loadLastBook(Core& core) {
  // Reset cover state
  coverBmpPath_.clear();
  hasCoverImage_ = false;
  coverLoadFailed_ = false;
  coverRendered_ = false;
  freeCoverThumbnail();

  // If content already open, use it
  if (core.content.isOpen()) {
    const auto& meta = core.content.metadata();
    view_.setBook(meta.title, meta.author, core.buf.path);

    // Check for existing thumbnail or cover (no async generation - ReaderState handles that)
    if (core.settings.showImages) {
      coverBmpPath_ = core.content.getThumbnailPath();
      if (!coverBmpPath_.empty() && SdMan.exists(coverBmpPath_.c_str())) {
        hasCoverImage_ = true;
        Serial.printf("[%lu] [HOME] Using cached thumbnail: %s\n", millis(), coverBmpPath_.c_str());
      }
    }
    view_.hasCoverBmp = hasCoverImage_;
    return;
  }

  // Try to load from saved path in settings
  const char* savedPath = core.settings.lastBookPath;
  if (savedPath[0] != '\0' && core.storage.exists(savedPath)) {
    // Open temporarily to get metadata
    auto result = core.content.open(savedPath, PAPYRIX_CACHE_DIR);
    if (result.ok()) {
      const auto& meta = core.content.metadata();
      view_.setBook(meta.title, meta.author, savedPath);
      // Set path in buf for "Continue Reading" button
      strncpy(core.buf.path, savedPath, sizeof(core.buf.path) - 1);
      core.buf.path[sizeof(core.buf.path) - 1] = '\0';

      // Check for existing thumbnail or cover (no async generation - ReaderState handles that)
      if (core.settings.showImages) {
        coverBmpPath_ = core.content.getThumbnailPath();
        if (!coverBmpPath_.empty() && SdMan.exists(coverBmpPath_.c_str())) {
          hasCoverImage_ = true;
          Serial.printf("[%lu] [HOME] Using cached thumbnail: %s\n", millis(), coverBmpPath_.c_str());
        }
      }
      view_.hasCoverBmp = hasCoverImage_;

      // Close to free memory (will reopen when user selects Continue Reading)
      core.content.close();
    } else {
      view_.clearBook();
    }
  } else {
    view_.clearBook();
  }
}

void HomeState::updateBattery() {
  int percent = batteryMonitor.readPercentage();
  view_.setBattery(percent);
}

StateTransition HomeState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
    switch (e.type) {
      case EventType::ButtonPress:
        switch (e.button) {
          case Button::Back:
            // btn1: Read - Continue reading if book is open
            if (view_.buttons.isActive(0) && view_.hasBook) {
              showTransitionNotification("Opening book...");
              saveTransition(BootMode::READER, core.buf.path, ReturnTo::HOME);
              vTaskDelay(50 / portTICK_PERIOD_MS);
              ESP.restart();
            }
            break;

          case Button::Center:
            // btn2: Files
            return StateTransition::to(StateId::FileList);

          case Button::Left:
            // btn3: Sync
            return StateTransition::to(StateId::Sync);

          case Button::Right:
            // btn4: Settings
            return StateTransition::to(StateId::Settings);

          case Button::Up:
          case Button::Down:
          case Button::Power:
            // Side buttons unused on home screen
            break;
        }
        break;

      case EventType::ButtonLongPress:
        if (e.button == Button::Power) {
          return StateTransition::to(StateId::Sleep);
        }
        break;

      default:
        break;
    }
  }

  return StateTransition::stay(StateId::Home);
}

void HomeState::render(Core& core) {
  if (!view_.needsRender) {
    return;
  }

  const Theme& theme = THEME;

  // If we have a stored compressed thumbnail, restore it instead of re-reading from SD
  const bool bufferRestored = coverBufferStored_ && restoreCoverThumbnail();

  // When cover is present, HomeState handles clear and card border
  // so cover can be drawn before text boxes
  if (hasCoverImage_ && !coverLoadFailed_) {
    const auto card = ui::CardDimensions::calculate(renderer_.getScreenWidth(), renderer_.getScreenHeight());

    if (!bufferRestored) {
      renderer_.clearScreen(theme.backgroundColor);

      // Render cover inside card (first time only)
      if (!coverRendered_) {
        // Clear cover area with white so bitmap is visible in dark mode
        const auto coverArea = card.getCoverArea();
        renderer_.clearArea(coverArea.x, coverArea.y, coverArea.width, coverArea.height, 0xFF);

        renderCoverToCard();
        if (!coverLoadFailed_) {
          // Store compressed thumbnail after first successful render
          coverBufferStored_ = storeCoverThumbnail();
          coverRendered_ = true;
        }
      }
    }
  }

  // Resolve external font for title/author (may trigger SD load on first call)
  view_.titleFontId = (theme.readerFontFamilySmall[0] != '\0')
                          ? FONT_MANAGER.getFontId(theme.readerFontFamilySmall, theme.uiFontId)
                          : theme.uiFontId;

  // Render rest of UI (text boxes will draw on top of cover)
  ui::render(renderer_, theme, view_);

  renderer_.displayBuffer();
  view_.needsRender = false;
  core.display.markDirty();
}

void HomeState::renderCoverToCard() {
  FsFile file;
  if (!SdMan.openFileForRead("HOME", coverBmpPath_, file)) {
    coverLoadFailed_ = true;
    Serial.printf("[%lu] [HOME] Failed to open cover BMP: %s\n", millis(), coverBmpPath_.c_str());
    return;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    coverLoadFailed_ = true;
    Serial.printf("[%lu] [HOME] Failed to parse cover BMP: %s\n", millis(), coverBmpPath_.c_str());
    return;
  }

  const auto card = ui::CardDimensions::calculate(renderer_.getScreenWidth(), renderer_.getScreenHeight());
  const auto coverArea = card.getCoverArea();
  auto rect = CoverHelpers::calculateCenteredRect(bitmap.getWidth(), bitmap.getHeight(), coverArea.x, coverArea.y,
                                                  coverArea.width, coverArea.height);

  renderer_.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
  file.close();
}

bool HomeState::storeCoverThumbnail() {
  uint8_t* frameBuffer = renderer_.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing thumbnail first
  freeCoverThumbnail();

  // Calculate cover area position (same logic as renderCoverToCard)
  const auto card = ui::CardDimensions::calculate(renderer_.getScreenWidth(), renderer_.getScreenHeight());
  const auto coverArea = card.getCoverArea();

  // Verify cover area is large enough for thumbnail
  if (coverArea.width < COVER_CACHE_WIDTH || coverArea.height < COVER_CACHE_HEIGHT) {
    Serial.println("[HOME] Cover area too small for thumbnail");
    return false;
  }

  // Use center of cover area for thumbnail extraction
  // Thumbnail is smaller than cover area, so center it
  const int srcX = coverArea.x + (coverArea.width - COVER_CACHE_WIDTH) / 2;
  const int srcY = coverArea.y + (coverArea.height - COVER_CACHE_HEIGHT) / 2;

  // Clamp to valid framebuffer bounds
  const int screenWidth = renderer_.getScreenWidth();
  const int screenHeight = renderer_.getScreenHeight();
  if (srcX < 0 || srcY < 0 || srcX + COVER_CACHE_WIDTH > screenWidth || srcY + COVER_CACHE_HEIGHT > screenHeight) {
    Serial.println("[HOME] Thumbnail position out of bounds");
    return false;
  }

  // Store position for restoration
  thumbX_ = static_cast<int16_t>(srcX);
  thumbY_ = static_cast<int16_t>(srcY);

  // Extract thumbnail region from framebuffer and compress with Group5
  // Framebuffer is 1-bit packed (8 pixels per byte), row-major order
  const int screenWidthBytes = screenWidth / 8;
  const int thumbWidthBytes = (COVER_CACHE_WIDTH + 7) / 8;
  const size_t thumbUncompressedSize = thumbWidthBytes * COVER_CACHE_HEIGHT;

  // For non-aligned access, we read one extra byte per row
  const int srcBitOffset = srcX % 8;
  const int srcByteX = srcX / 8;
  const int bytesNeeded = thumbWidthBytes + (srcBitOffset != 0 ? 1 : 0);
  if (srcByteX + bytesNeeded > screenWidthBytes) {
    Serial.println("[HOME] Insufficient source bytes for thumbnail extraction");
    return false;
  }

  // Allocate temporary buffer for uncompressed thumbnail
  uint8_t* thumbBuffer = static_cast<uint8_t*>(malloc(thumbUncompressedSize));
  if (!thumbBuffer) {
    Serial.println("[HOME] Failed to allocate temp thumbnail buffer");
    return false;
  }

  // Extract thumbnail region from framebuffer
  // Handle non-byte-aligned X position by bit-shifting
  for (int row = 0; row < COVER_CACHE_HEIGHT; row++) {
    const uint8_t* srcRow = frameBuffer + (srcY + row) * screenWidthBytes + srcByteX;
    uint8_t* dstRow = thumbBuffer + row * thumbWidthBytes;

    if (srcBitOffset == 0) {
      // Byte-aligned: direct copy
      memcpy(dstRow, srcRow, thumbWidthBytes);
    } else {
      // Non-aligned: need to shift bits
      for (int col = 0; col < thumbWidthBytes; col++) {
        uint8_t hi = srcRow[col];
        uint8_t lo = srcRow[col + 1];
        dstRow[col] = (hi << srcBitOffset) | (lo >> (8 - srcBitOffset));
      }
    }
  }

  // Allocate output buffer for compressed data
  compressedThumb_ = static_cast<uint8_t*>(malloc(MAX_COVER_CACHE_SIZE));
  if (!compressedThumb_) {
    free(thumbBuffer);
    Serial.println("[HOME] Failed to allocate compressed thumbnail buffer");
    return false;
  }

  // Compress using Group5
  G5ENCODER encoder;
  if (encoder.init(COVER_CACHE_WIDTH, COVER_CACHE_HEIGHT, compressedThumb_, MAX_COVER_CACHE_SIZE) != G5_SUCCESS) {
    free(thumbBuffer);
    free(compressedThumb_);
    compressedThumb_ = nullptr;
    compressedSize_ = 0;
    Serial.println("[HOME] Group5 encoder init failed");
    return false;
  }

  for (int row = 0; row < COVER_CACHE_HEIGHT; row++) {
    int result = encoder.encodeLine(thumbBuffer + row * thumbWidthBytes);
    if (result != G5_SUCCESS && result != G5_ENCODE_COMPLETE) {
      free(thumbBuffer);
      free(compressedThumb_);
      compressedThumb_ = nullptr;
      compressedSize_ = 0;
      Serial.printf("[HOME] Group5 encode failed at row %d\n", row);
      return false;
    }
  }

  compressedSize_ = encoder.size();
  free(thumbBuffer);

  // Verify compressed size fits in allocated buffer
  if (compressedSize_ > MAX_COVER_CACHE_SIZE) {
    Serial.printf("[HOME] Compressed size %zu exceeds max %zu\n", compressedSize_, MAX_COVER_CACHE_SIZE);
    free(compressedThumb_);
    compressedThumb_ = nullptr;
    compressedSize_ = 0;
    return false;
  }

  Serial.printf("[HOME] Stored compressed thumbnail (%zu -> %zu bytes, %.1f%% ratio)\n", thumbUncompressedSize,
                compressedSize_, 100.0f * compressedSize_ / thumbUncompressedSize);
  return true;
}

bool HomeState::restoreCoverThumbnail() {
  if (!compressedThumb_ || compressedSize_ == 0) {
    return false;
  }

  uint8_t* frameBuffer = renderer_.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // First, clear screen (text will be redrawn by ui::render)
  const Theme& theme = THEME;

  renderer_.clearScreen(theme.backgroundColor);

  // Decode compressed thumbnail
  const int thumbWidthBytes = (COVER_CACHE_WIDTH + 7) / 8;
  const size_t thumbUncompressedSize = thumbWidthBytes * COVER_CACHE_HEIGHT;
  uint8_t* thumbBuffer = static_cast<uint8_t*>(malloc(thumbUncompressedSize));
  if (!thumbBuffer) {
    Serial.println("[HOME] Failed to allocate decompress buffer");
    return false;
  }

  G5DECODER decoder;
  if (decoder.init(COVER_CACHE_WIDTH, COVER_CACHE_HEIGHT, compressedThumb_, compressedSize_) != G5_SUCCESS) {
    free(thumbBuffer);
    Serial.println("[HOME] Group5 decoder init failed");
    return false;
  }

  for (int row = 0; row < COVER_CACHE_HEIGHT; row++) {
    int result = decoder.decodeLine(thumbBuffer + row * thumbWidthBytes);
    if (result != G5_SUCCESS && result != G5_DECODE_COMPLETE) {
      free(thumbBuffer);
      Serial.printf("[HOME] Group5 decode failed at row %d\n", row);
      return false;
    }
  }

  // Write thumbnail back to framebuffer at saved position
  const int screenWidth = renderer_.getScreenWidth();
  const int screenHeight = renderer_.getScreenHeight();
  const int screenWidthBytes = screenWidth / 8;
  const int dstBitOffset = thumbX_ % 8;
  const int dstByteX = thumbX_ / 8;

  // Validate saved position is still within bounds
  if (thumbX_ < 0 || thumbY_ < 0 || thumbX_ + COVER_CACHE_WIDTH > screenWidth ||
      thumbY_ + COVER_CACHE_HEIGHT > screenHeight) {
    free(thumbBuffer);
    Serial.println("[HOME] Thumbnail position out of bounds for restore");
    return false;
  }

  // For non-aligned access, we write one extra byte per row
  const int bytesNeeded = thumbWidthBytes + (dstBitOffset != 0 ? 1 : 0);
  if (dstByteX + bytesNeeded > screenWidthBytes) {
    free(thumbBuffer);
    Serial.println("[HOME] Insufficient destination bytes for thumbnail restore");
    return false;
  }

  for (int row = 0; row < COVER_CACHE_HEIGHT; row++) {
    uint8_t* dstRow = frameBuffer + (thumbY_ + row) * screenWidthBytes + dstByteX;
    const uint8_t* srcRow = thumbBuffer + row * thumbWidthBytes;

    if (dstBitOffset == 0) {
      // Byte-aligned: direct copy
      memcpy(dstRow, srcRow, thumbWidthBytes);
    } else {
      // Non-aligned: need to shift bits and merge
      for (int col = 0; col < thumbWidthBytes; col++) {
        uint8_t srcByte = srcRow[col];
        // Merge into destination, preserving bits outside thumbnail
        uint8_t mask1 = 0xFF >> dstBitOffset;
        uint8_t mask2 = 0xFF << (8 - dstBitOffset);

        dstRow[col] = (dstRow[col] & ~mask1) | (srcByte >> dstBitOffset);
        dstRow[col + 1] = (dstRow[col + 1] & ~mask2) | (srcByte << (8 - dstBitOffset));
      }
    }
  }

  free(thumbBuffer);
  return true;
}

void HomeState::freeCoverThumbnail() {
  if (compressedThumb_) {
    free(compressedThumb_);
    compressedThumb_ = nullptr;
  }
  compressedSize_ = 0;
  coverBufferStored_ = false;
}

}  // namespace papyrix
