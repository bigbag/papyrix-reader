#include "HomeState.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <Fb2.h>
#include <GfxRenderer.h>
#include <HomeThumbnail.h>
#include <I18n.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <esp_system.h>

#include "../config.h"
#include "../content/ContentTypes.h"
#include "../core/BootMode.h"
#include "../core/Core.h"
#include "../core/CrashDebug.h"
#include "../drivers/Device.h"
#include "Battery.h"
#include "FontManager.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"

#define TAG "HOME"

namespace papyrix {

HomeState::HomeState(GfxRenderer& renderer) : renderer_(renderer) {}

void HomeState::enter(Core& core) {
  LOG_INF(TAG, "Entering");

  // Load last book info if content is still open
  loadLastBook(core);

  // Update battery
  updateBattery();
  lastBatteryPollMs_ = millis();

  view_.needsRender = true;
}

void HomeState::exit(Core& core) {
  LOG_INF(TAG, "Exiting");
  view_.clear();
}

void HomeState::loadLastBook(Core& core) {
  // Reset cover state
  homeImage_ = {};
  hasCoverImage_ = false;
  coverLoadFailed_ = false;

  // If content already open, use it
  if (core.content.isOpen()) {
    const auto& meta = core.content.metadata();
    view_.setBook(meta.title, meta.author, core.buf.path);

    // ReaderState generates missing thumbnails during idle time.
    selectHomeImage(core.settings.showImages, core.content.getThumbnailPath(), core.content.getCoverPath());
    return;
  }

  // Try to load from saved path in settings
  const char* savedPath = core.settings.lastBookPath;
  auto existsResult = core.storage.exists(savedPath);
  if (savedPath[0] != '\0' && existsResult.ok() && *existsResult) {
    if (papyrix::crashdebug::shouldSkipHomeMetadata()) {
      LOG_INF(TAG, "Skipping metadata load after previous crash");
      view_.clearBook();
      return;
    }

    const auto contentType = detectContentType(savedPath);

    if (contentType == ContentType::Epub) {
      // EPUB: lightweight metadata-only load (no CSS, TOC, spine splitting)
      papyrix::crashdebug::mark(papyrix::crashdebug::CrashPhase::HomeMetadataLoad);
      Epub epub(savedPath, papyrix::drivers::Device::instance().cacheDir());
      if (epub.loadMetadataOnly()) {
        papyrix::crashdebug::clear();
        view_.setBook(epub.getTitle().c_str(), epub.getAuthor().c_str(), savedPath);
        strncpy(core.buf.path, savedPath, sizeof(core.buf.path) - 1);
        core.buf.path[sizeof(core.buf.path) - 1] = '\0';

        selectHomeImage(core.settings.showImages, home_thumbnail::pathForCache(epub.getCachePath()),
                        home_thumbnail::coverPathForCache(epub.getCachePath()));
      } else {
        papyrix::crashdebug::clear();
        view_.clearBook();
      }
    } else if (contentType == ContentType::Fb2) {
      // FB2: lightweight metadata-only load (no section scanning/file generation)
      papyrix::crashdebug::mark(papyrix::crashdebug::CrashPhase::HomeMetadataLoad);
      Fb2 fb2(savedPath, papyrix::drivers::Device::instance().cacheDir());
      if (fb2.loadMetadataOnly()) {
        papyrix::crashdebug::clear();
        view_.setBook(fb2.getTitle().c_str(), fb2.getAuthor().c_str(), savedPath);
        strncpy(core.buf.path, savedPath, sizeof(core.buf.path) - 1);
        core.buf.path[sizeof(core.buf.path) - 1] = '\0';

        selectHomeImage(core.settings.showImages, home_thumbnail::pathForCache(fb2.getCachePath()),
                        home_thumbnail::coverPathForCache(fb2.getCachePath()));
      } else {
        papyrix::crashdebug::clear();
        view_.clearBook();
      }
    } else {
      // Non-EPUB/FB2: use full content pipeline (fast for TXT/Markdown)
      papyrix::crashdebug::mark(papyrix::crashdebug::CrashPhase::HomeMetadataLoad);
      auto result = core.content.open(savedPath, papyrix::drivers::Device::instance().cacheDir());
      papyrix::crashdebug::clear();
      if (result.ok()) {
        const auto& meta = core.content.metadata();
        view_.setBook(meta.title, meta.author, savedPath);
        strncpy(core.buf.path, savedPath, sizeof(core.buf.path) - 1);
        core.buf.path[sizeof(core.buf.path) - 1] = '\0';

        selectHomeImage(core.settings.showImages, core.content.getThumbnailPath(), core.content.getCoverPath());
        core.content.close();
      } else {
        view_.clearBook();
      }
    }
  } else {
    view_.clearBook();
  }
}

void HomeState::selectHomeImage(const bool imagesEnabled, const std::string& thumbnailPath,
                                const std::string& coverPath) {
  homeImage_ = home_thumbnail::selectForHome(imagesEnabled, thumbnailPath, coverPath);
  hasCoverImage_ = homeImage_.type != home_thumbnail::HomeImageType::None;
  view_.hasCoverBmp = hasCoverImage_;

  if (homeImage_.type == home_thumbnail::HomeImageType::Thumbnail) {
    LOG_DBG(TAG, "Using cached Home thumbnail: %s", homeImage_.path.c_str());
  } else if (homeImage_.type == home_thumbnail::HomeImageType::Cover) {
    LOG_DBG(TAG, "Using Home cover fallback: %s", homeImage_.path.c_str());
  }
}

void HomeState::updateBattery() {
  int percent = batteryMonitor.readSmoothedPercentage();
  view_.setBattery(percent);
  view_.setBatteryCharging(isUsbConnected());
}

StateTransition HomeState::update(Core& core) {
  const unsigned long now = millis();
  if (now - lastBatteryPollMs_ >= kBatteryPollIntervalMs) {
    lastBatteryPollMs_ = now;
    updateBattery();
  }

  Event e;
  while (core.events.pop(e)) {
    switch (e.type) {
      case EventType::ButtonPress:
        switch (e.button) {
          case Button::Back:
            // btn1: Read - Continue reading if book is open
            if (view_.hasBook) {
              showTransitionNotification(tr(OPENING_BOOK));
              saveTransition(BootMode::READER, core.buf.path, ReturnTo::HOME);
              vTaskDelay(50 / portTICK_PERIOD_MS);
              ESP.restart();
            }
            break;

          case Button::Center:
            // btn2: Books (Recent) or Files, per Show Recents setting
            return StateTransition::to(core.settings.showRecents ? StateId::Recent : StateId::FileList);

          case Button::Left:
            // btn3: Apps
            return StateTransition::to(StateId::AppLauncher);

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
  // Battery-only update: redraw just the battery region and partial-refresh it
  if (!view_.needsRender && view_.batteryNeedsRender) {
    const auto region = ui::renderBatteryOnly(renderer_, THEME, view_);
    renderer_.displayWindow(region.x, region.y, region.width, region.height);
    view_.batteryNeedsRender = false;
    core.display.markDirty();
    return;
  }

  if (!view_.needsRender) {
    return;
  }

  const Theme& theme = THEME;

  if (hasCoverImage_ && !coverLoadFailed_) {
    const auto card = ui::CardDimensions::calculate(renderer_.getScreenWidth(), renderer_.getScreenHeight());
    const auto coverArea = card.getCoverArea();
    renderer_.clearScreen(theme.backgroundColor);
    renderer_.clearArea(coverArea.x, coverArea.y, coverArea.width, coverArea.height, 0xFF);
    if (!renderCoverToCard()) {
      coverLoadFailed_ = true;
      hasCoverImage_ = false;
      view_.hasCoverBmp = false;
    }
  }

  // Resolve external font for title/author (may trigger SD load on first call)
  view_.titleFontId = (theme.readerFontFamilySmall[0] != '\0')
                          ? FONT_MANAGER.getFontId(theme.readerFontFamilySmall, theme.uiFontId)
                          : theme.uiFontId;

  // Render rest of UI (text boxes will draw on top of cover)
  view_.showRecents = core.settings.showRecents;
  ui::render(renderer_, theme, view_);

  renderer_.displayBuffer();
  view_.needsRender = false;
  view_.batteryNeedsRender = false;
  core.display.markDirty();
}

bool HomeState::renderCoverToCard() {
  FsFile file;
  if (!SdMan.openFileForRead("HOME", homeImage_.path, file)) {
    LOG_ERR(TAG, "Failed to open Home image: %s", homeImage_.path.c_str());
    return false;
  }

  Bitmap bitmap(file);
  const bool parsed = bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.hasCompletePixelData() &&
                      bitmap.getBpp() == 1 && bitmap.isTopDown();
  const bool validThumbnail = homeImage_.type == home_thumbnail::HomeImageType::Thumbnail &&
                              bitmap.getWidth() <= home_thumbnail::MAX_WIDTH &&
                              bitmap.getHeight() <= home_thumbnail::MAX_HEIGHT;
  const bool validCover = homeImage_.type == home_thumbnail::HomeImageType::Cover;
  if (!parsed || (!validThumbnail && !validCover)) {
    file.close();
    LOG_ERR(TAG, "Invalid Home image: %s", homeImage_.path.c_str());
    return false;
  }

  const auto card = ui::CardDimensions::calculate(renderer_.getScreenWidth(), renderer_.getScreenHeight());
  const auto coverArea = card.getCoverArea();
  int drawWidth = bitmap.getWidth();
  int drawHeight = bitmap.getHeight();
  if (homeImage_.type == home_thumbnail::HomeImageType::Cover) {
    home_thumbnail::fitDimensions(bitmap.getWidth(), bitmap.getHeight(), drawWidth, drawHeight);
  }
  const int x = coverArea.x + (coverArea.width - drawWidth) / 2;
  const int y = coverArea.y + (coverArea.height - drawHeight) / 2;
  const bool isCover = homeImage_.type == home_thumbnail::HomeImageType::Cover;
  renderer_.drawBitmap(bitmap, x, y, isCover ? coverArea.width : drawWidth, isCover ? coverArea.height : drawHeight);
  file.close();
  return true;
}

}  // namespace papyrix
