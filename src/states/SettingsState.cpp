#include "SettingsState.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HardwareIdentity.h>
#include <I18n.h>
#include <LittleFS.h>  // Must be before SdFat includes to avoid FILE_READ/FILE_WRITE redefinition
#include <Logging.h>
#include <SDCardManager.h>

#include <algorithm>

#include "../content/RecentBooksStore.h"
#include "../core/FirmwareUpdater.h"
#include "../core/TrashPaths.h"

#define TAG "SETTINGS_UI"
#include "../config.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"

namespace papyrix {

static_assert(ui::ScreenSettingsView::MAX_THEMES == MAX_CACHED_THEMES,
              "Theme manager and settings view capacities must match");

SettingsState::SettingsState(GfxRenderer& renderer)
    : renderer_(renderer),
      core_(nullptr),
      currentScreen_(SettingsScreen::Menu),
      needsRender_(true),
      goHome_(false),
      goNetwork_(false),
      themeWasChanged_(false),
      returnScreen_(SettingsScreen::Menu),
      pendingAction_(ACTION_NONE),
      menuView_{},
      readerView_{},
      screenView_{},
      deviceView_{},
      cleanupView_{},
      confirmView_{},
      firmwareView_{},
      infoView_{} {}

SettingsState::~SettingsState() = default;

void SettingsState::enter(Core& core) {
  LOG_INF(TAG, "Entering");
  core_ = &core;  // Store for helper methods
  currentScreen_ = returnScreen_;
  returnScreen_ = SettingsScreen::Menu;  // Reset for next normal entry

  ui::ReaderSettingsView::initDefs();
  ui::ScreenSettingsView::initDefs();
  ui::DeviceSettingsView::initDefs();

  // Reset all views to ensure clean state
  menuView_.selected = 0;
  menuView_.needsRender = true;
  readerView_.selected = 0;
  readerView_.needsRender = true;
  screenView_.selected = 0;
  screenView_.needsRender = true;
  deviceView_.selected = 0;
  deviceView_.needsRender = true;
  if (currentScreen_ == SettingsScreen::Device) loadDeviceSettings();
  cleanupView_.selected = 0;
  cleanupView_.needsRender = true;
  confirmView_.needsRender = true;
  firmwareView_ = ui::FirmwareUpdateView{};
  lastFirmwareRenderMs_ = 0;
  firmwareCompleteMs_ = 0;
  infoView_.clear();
  infoView_.needsRender = true;

  needsRender_ = true;
  goHome_ = false;
  goNetwork_ = false;
  themeWasChanged_ = false;
  pendingAction_ = ACTION_NONE;
}

void SettingsState::exit(Core& core) {
  LOG_INF(TAG, "Exiting");
  // Save settings on exit
  core.settings.save(core.storage);
}

void SettingsState::handleTap(Core& core, const Event& event) {
  const int width = renderer_.getScreenWidth();
  const int height = renderer_.getScreenHeight();
  const int rowHeight = THEME.itemHeight + THEME.itemSpacing;
  const bool frontLrbc = core.settings.frontButtonLayout == Settings::FrontLRBC;
  int rowCount = 0;
  if (currentScreen_ == SettingsScreen::Menu) rowCount = ui::SettingsMenuView::ITEM_COUNT;
  if (currentScreen_ == SettingsScreen::Reader) rowCount = readerView_.visibleCount;
  if (currentScreen_ == SettingsScreen::Screen) rowCount = screenView_.visibleCount;
  if (currentScreen_ == SettingsScreen::Device) rowCount = deviceView_.visibleCount;
  if (currentScreen_ == SettingsScreen::Cleanup) rowCount = ui::CleanupMenuView::ITEM_COUNT;

  if (currentScreen_ == SettingsScreen::ConfirmDialog) {
    const auto layout = ui::confirmDialogBounds(renderer_, THEME, confirmView_);
    const auto hit = confirmView_.hitTest({event.touch.x, event.touch.y}, layout, frontLrbc);
    if (hit == ui::ConfirmDialogView::Hit::Yes) {
      confirmView_.selection = 0;
      handleConfirm(core);
    } else if (hit == ui::ConfirmDialogView::Hit::No) {
      confirmView_.selection = 1;
      handleConfirm(core);
    } else if (hit == ui::ConfirmDialogView::Hit::Select) {
      handleConfirm(core);
    } else if (hit == ui::ConfirmDialogView::Hit::Back) {
      goBack(core);
    }
    return;
  }

  const auto hit =
      ui::settingsListHitTest({event.touch.x, event.touch.y}, width, height, rowHeight, rowCount, frontLrbc);
  if (hit.type == ui::SettingsListHit::Type::Row) {
    if (currentScreen_ == SettingsScreen::Menu) menuView_.selected = static_cast<int8_t>(hit.index);
    if (currentScreen_ == SettingsScreen::Reader) readerView_.selected = static_cast<int8_t>(hit.index);
    if (currentScreen_ == SettingsScreen::Screen) screenView_.selected = static_cast<int8_t>(hit.index);
    if (currentScreen_ == SettingsScreen::Device) deviceView_.selected = static_cast<int8_t>(hit.index);
    if (currentScreen_ == SettingsScreen::Cleanup) cleanupView_.selected = static_cast<int8_t>(hit.index);
    needsRender_ = true;
    if (currentScreen_ == SettingsScreen::Menu || currentScreen_ == SettingsScreen::Cleanup ||
        (currentScreen_ == SettingsScreen::Device && deviceView_.selected == 0))
      handleConfirm(core);
  } else if (hit.type == ui::SettingsListHit::Type::Previous &&
             (currentScreen_ == SettingsScreen::Reader || currentScreen_ == SettingsScreen::Screen ||
              currentScreen_ == SettingsScreen::Device)) {
    handleLeftRight(-1);
  } else if (hit.type == ui::SettingsListHit::Type::Next &&
             (currentScreen_ == SettingsScreen::Reader || currentScreen_ == SettingsScreen::Screen ||
              currentScreen_ == SettingsScreen::Device)) {
    handleLeftRight(1);
  } else if (hit.type == ui::SettingsListHit::Type::Open &&
             (currentScreen_ == SettingsScreen::Menu || currentScreen_ == SettingsScreen::Cleanup ||
              currentScreen_ == SettingsScreen::FirmwareUpdate ||
              (currentScreen_ == SettingsScreen::Device && deviceView_.selected == 0))) {
    handleConfirm(core);
  } else if (hit.type == ui::SettingsListHit::Type::Back) {
    if (currentScreen_ == SettingsScreen::Menu) {
      core.settings.save(core.storage);
      goHome_ = true;
    } else if (currentScreen_ != SettingsScreen::FirmwareUpdate ||
               (firmwareView_.state != ui::FirmwareUpdateView::State::Flashing &&
                firmwareView_.state != ui::FirmwareUpdateView::State::Validating)) {
      goBack(core);
    }
  }
}

StateTransition SettingsState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
    if (e.type == EventType::Tap) {
      handleTap(core, e);
      continue;
    }
    switch (e.type) {
      case EventType::ButtonRepeat:
      case EventType::ButtonPress:
        switch (e.button) {
          case Button::Up:
            switch (currentScreen_) {
              case SettingsScreen::Menu:
                menuView_.moveUp();
                break;
              case SettingsScreen::Reader:
                readerView_.moveUp();
                break;
              case SettingsScreen::Screen:
                screenView_.moveUp();
                break;
              case SettingsScreen::Device:
                deviceView_.moveUp();
                break;
              case SettingsScreen::Cleanup:
                cleanupView_.moveUp();
                break;
              case SettingsScreen::ConfirmDialog:
                confirmView_.toggleSelection();
                break;
              default:
                break;
            }
            needsRender_ = true;
            break;

          case Button::Down:
            switch (currentScreen_) {
              case SettingsScreen::Menu:
                menuView_.moveDown();
                break;
              case SettingsScreen::Reader:
                readerView_.moveDown();
                break;
              case SettingsScreen::Screen:
                screenView_.moveDown();
                break;
              case SettingsScreen::Device:
                deviceView_.moveDown();
                break;
              case SettingsScreen::Cleanup:
                cleanupView_.moveDown();
                break;
              case SettingsScreen::ConfirmDialog:
                confirmView_.toggleSelection();
                break;
              default:
                break;
            }
            needsRender_ = true;
            break;

          case Button::Left:
            switch (currentScreen_) {
              case SettingsScreen::Reader:
                if (readerView_.buttons.isActive(2)) handleLeftRight(-1);
                break;
              case SettingsScreen::Screen:
                if (screenView_.buttons.isActive(2)) handleLeftRight(-1);
                break;
              case SettingsScreen::Device:
                if (deviceView_.buttons.isActive(2)) handleLeftRight(-1);
                break;
              case SettingsScreen::ConfirmDialog:
                confirmView_.toggleSelection();
                needsRender_ = true;
                break;
            }
            break;

          case Button::Right:
            switch (currentScreen_) {
              case SettingsScreen::Reader:
                if (readerView_.buttons.isActive(3)) handleLeftRight(+1);
                break;
              case SettingsScreen::Screen:
                if (screenView_.buttons.isActive(3)) handleLeftRight(+1);
                break;
              case SettingsScreen::Device:
                if (deviceView_.buttons.isActive(3)) handleLeftRight(+1);
                break;
              case SettingsScreen::ConfirmDialog:
                confirmView_.toggleSelection();
                needsRender_ = true;
                break;
              default:
                break;
            }
            break;

          case Button::Center:
            handleConfirm(core);
            break;

          case Button::Back:
            if (currentScreen_ == SettingsScreen::Menu) {
              core.settings.save(core.storage);
              goHome_ = true;
            } else if (currentScreen_ == SettingsScreen::ConfirmDialog) {
              // Cancel confirmation dialog
              pendingAction_ = ACTION_NONE;
              currentScreen_ = SettingsScreen::Cleanup;
              cleanupView_.needsRender = true;
              needsRender_ = true;
            } else if (currentScreen_ == SettingsScreen::FirmwareUpdate) {
              if (firmwareView_.state == ui::FirmwareUpdateView::State::Flashing ||
                  firmwareView_.state == ui::FirmwareUpdateView::State::Validating) {
                // Back is disabled during validation/flashing
              } else {
                goBack(core);
              }
            } else {
              goBack(core);
            }
            break;

          case Button::Power:
            break;
        }
        break;

      default:
        break;
    }
  }

  if (goNetwork_) {
    goNetwork_ = false;
    core.settings.save(core.storage);
    return StateTransition::to(StateId::Network);
  }

  // Firmware validation (runs after "Validating..." frame is rendered)
  if (currentScreen_ == SettingsScreen::FirmwareUpdate &&
      firmwareView_.state == ui::FirmwareUpdateView::State::Validating && firmwareValidationRendered_) {
    if (!FW_UPDATER.beginUpdate()) {
      firmwareView_.state = ui::FirmwareUpdateView::State::Error;
      snprintf(
          firmwareView_.statusLine, sizeof(firmwareView_.statusLine), "%s",
          FW_UPDATER.progress().error == FirmwareUpdateError::BadChip ? tr(UPDATE_WRONG_DEVICE) : tr(UPDATE_FAILED));
    } else {
      firmwareView_.state = ui::FirmwareUpdateView::State::Flashing;
      snprintf(firmwareView_.statusLine, sizeof(firmwareView_.statusLine), "%s", tr(FLASHING_UPDATE));
      lastFirmwareRenderMs_ = 0;
    }
    firmwareView_.needsRender = true;
    needsRender_ = true;
  }

  // Firmware update pump (runs each frame while flashing)
  if (currentScreen_ == SettingsScreen::FirmwareUpdate &&
      firmwareView_.state == ui::FirmwareUpdateView::State::Flashing) {
    if (!FW_UPDATER.pump()) {
      const auto& p = FW_UPDATER.progress();
      if (p.phase == FirmwareUpdatePhase::Complete) {
        firmwareView_.state = ui::FirmwareUpdateView::State::Complete;
        snprintf(firmwareView_.statusLine, sizeof(firmwareView_.statusLine), "%s", tr(UPDATE_COMPLETE));
        firmwareCompleteMs_ = millis();
      } else {
        firmwareView_.state = ui::FirmwareUpdateView::State::Error;
        snprintf(firmwareView_.statusLine, sizeof(firmwareView_.statusLine), "%s", tr(UPDATE_FAILED));
      }
      needsRender_ = true;
      firmwareView_.needsRender = true;
    } else {
      const auto& p = FW_UPDATER.progress();
      firmwareView_.progressPercent =
          p.totalBytes > 0 ? static_cast<int>(static_cast<int64_t>(p.bytesFlashed) * 100 / p.totalBytes) : 0;
      if (millis() - lastFirmwareRenderMs_ >= 3000) {
        lastFirmwareRenderMs_ = millis();
        needsRender_ = true;
        firmwareView_.needsRender = true;
      }
    }
  }

  // Auto-restart after showing "Update complete" for 2 seconds
  if (firmwareCompleteMs_ != 0 && millis() - firmwareCompleteMs_ >= 2000) {
    ESP.restart();
  }

  if (goHome_) {
    goHome_ = false;
    return StateTransition::to(StateId::Home);
  }

  return StateTransition::stay(StateId::Settings);
}

void SettingsState::render(Core& core) {
  if (!needsRender_) {
    bool viewNeedsRender = false;
    switch (currentScreen_) {
      case SettingsScreen::Menu:
        viewNeedsRender = menuView_.needsRender;
        break;
      case SettingsScreen::Reader:
        viewNeedsRender = readerView_.needsRender;
        break;
      case SettingsScreen::Screen:
        viewNeedsRender = screenView_.needsRender;
        break;
      case SettingsScreen::Device:
        viewNeedsRender = deviceView_.needsRender;
        break;
      case SettingsScreen::Cleanup:
        viewNeedsRender = cleanupView_.needsRender;
        break;
      case SettingsScreen::SystemInfo:
        viewNeedsRender = infoView_.needsRender;
        break;
      case SettingsScreen::ConfirmDialog:
        viewNeedsRender = confirmView_.needsRender;
        break;
      case SettingsScreen::FirmwareUpdate:
        viewNeedsRender = firmwareView_.needsRender;
        break;
    }
    if (!viewNeedsRender) {
      return;
    }
  }

  switch (currentScreen_) {
    case SettingsScreen::Menu:
      ui::render(renderer_, THEME, menuView_);
      menuView_.needsRender = false;
      break;
    case SettingsScreen::Reader:
      ui::render(renderer_, THEME, readerView_);
      readerView_.needsRender = false;
      break;
    case SettingsScreen::Screen:
      ui::render(renderer_, THEME, screenView_);
      screenView_.needsRender = false;
      break;
    case SettingsScreen::Device:
      ui::render(renderer_, THEME, deviceView_);
      deviceView_.needsRender = false;
      break;
    case SettingsScreen::Cleanup:
      ui::render(renderer_, THEME, cleanupView_);
      cleanupView_.needsRender = false;
      break;
    case SettingsScreen::SystemInfo:
      ui::render(renderer_, THEME, infoView_);
      infoView_.needsRender = false;
      break;
    case SettingsScreen::ConfirmDialog:
      ui::render(renderer_, THEME, confirmView_);
      confirmView_.needsRender = false;
      break;
    case SettingsScreen::FirmwareUpdate:
      ui::render(renderer_, THEME, firmwareView_);
      firmwareView_.needsRender = false;
      if (firmwareView_.state == ui::FirmwareUpdateView::State::Validating && !firmwareValidationRendered_) {
        firmwareValidationRendered_ = true;
        needsRender_ = true;
      }
      break;
  }

  needsRender_ = false;
}

void SettingsState::openSelected() {
  switch (menuView_.selectedItem()) {
    case ui::SettingsMenuView::Item::Reader:
      loadReaderSettings();
      readerView_.selected = 0;
      readerView_.needsRender = true;
      currentScreen_ = SettingsScreen::Reader;
      break;
    case ui::SettingsMenuView::Item::Screen:
      loadScreenSettings();
      screenView_.selected = 0;
      screenView_.needsRender = true;
      currentScreen_ = SettingsScreen::Screen;
      break;
    case ui::SettingsMenuView::Item::Device:
      loadDeviceSettings();
      deviceView_.selected = 0;
      deviceView_.needsRender = true;
      currentScreen_ = SettingsScreen::Device;
      break;
    case ui::SettingsMenuView::Item::Cleanup:
      cleanupView_.selected = 0;
      cleanupView_.needsRender = true;
      currentScreen_ = SettingsScreen::Cleanup;
      break;
    case ui::SettingsMenuView::Item::FirmwareUpdate:
      firmwareView_.state = ui::FirmwareUpdateView::State::Idle;
      firmwareView_.progressPercent = 0;
      firmwareView_.needsRender = true;
      lastFirmwareRenderMs_ = 0;
      if (FW_UPDATER.findFirmwareFile()) {
        snprintf(firmwareView_.statusLine, sizeof(firmwareView_.statusLine), "%s: %s", tr(FIRMWARE_FILE_FOUND),
                 PAPYRIX_FIRMWARE_FILE);
      } else {
        snprintf(firmwareView_.statusLine, sizeof(firmwareView_.statusLine), "%s", tr(NO_FIRMWARE_FILE));
      }
      currentScreen_ = SettingsScreen::FirmwareUpdate;
      break;
    case ui::SettingsMenuView::Item::SystemInfo:
      populateSystemInfo();
      infoView_.needsRender = true;
      currentScreen_ = SettingsScreen::SystemInfo;
      break;
    case ui::SettingsMenuView::Item::Count:
      break;
  }
  needsRender_ = true;
}

void SettingsState::goBack(Core& core) {
  switch (currentScreen_) {
    case SettingsScreen::Reader:
      saveReaderSettings();
      currentScreen_ = SettingsScreen::Menu;
      menuView_.needsRender = true;
      break;
    case SettingsScreen::Screen:
      saveScreenSettings();
      currentScreen_ = SettingsScreen::Menu;
      menuView_.needsRender = true;
      break;
    case SettingsScreen::Device:
      saveDeviceSettings();
      // Apply button layouts now that we're leaving the screen
      core.settings.frontButtonLayout = std::min(deviceView_.values[0], uint8_t(Settings::FrontLRBC));
      core.settings.sideButtonLayout = std::min(deviceView_.values[1], uint8_t(Settings::NextPrev));
      ui::setFrontButtonLayout(core.settings.frontButtonLayout);
      core.input.resyncState();
      currentScreen_ = SettingsScreen::Menu;
      menuView_.needsRender = true;
      break;
    case SettingsScreen::Cleanup:
    case SettingsScreen::SystemInfo:
      currentScreen_ = SettingsScreen::Menu;
      menuView_.needsRender = true;
      break;
    case SettingsScreen::ConfirmDialog:
      pendingAction_ = ACTION_NONE;
      currentScreen_ = SettingsScreen::Cleanup;
      cleanupView_.needsRender = true;
      break;
    case SettingsScreen::FirmwareUpdate:
      FW_UPDATER.abort();
      FW_UPDATER.reset();
      currentScreen_ = SettingsScreen::Menu;
      menuView_.needsRender = true;
      break;
    default:
      break;
  }
  needsRender_ = true;
}

void SettingsState::handleConfirm(Core& core) {
  switch (currentScreen_) {
    case SettingsScreen::Menu:
      openSelected();
      break;

    case SettingsScreen::Reader:
      readerView_.cycleValue(1);
      saveReaderSettings();
      needsRender_ = true;
      break;

    case SettingsScreen::Device:
      if (deviceView_.selected == 0) {
        returnScreen_ = SettingsScreen::Device;
        goBack(core);
        core.pendingSync = SyncMode::WifiSetup;
        goNetwork_ = true;
      } else {
        handleLeftRight(1);
      }
      break;
    case SettingsScreen::Screen:
      handleLeftRight(1);
      break;

    case SettingsScreen::Cleanup:
      clearCache(cleanupView_.selectedItem());
      break;

    case SettingsScreen::SystemInfo:
      goBack(core);
      break;

    case SettingsScreen::ConfirmDialog:
      if (confirmView_.isYesSelected()) {
        if (pendingAction_ == ACTION_CLEAR_BOOK_CACHE) {
          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(CLEARING_CACHE));

          int lastRendered = 0;
          auto result = core.storage.rmdir(PAPYRIX_CACHE_DIR, [this, &lastRendered](int n) {
            if (n - lastRendered < 50) return;
            lastRendered = n;
            char buf[64];
            snprintf(buf, sizeof(buf), "%s (%d)", tr(CLEARING_CACHE), n);
            ui::centeredMessage(renderer_, THEME, THEME.uiFontId, buf);
          });

          const char* msg = result.ok() ? tr(CACHE_CLEARED) : tr(NO_CACHE_TO_CLEAR);
          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, msg);
          vTaskDelay(1500 / portTICK_PERIOD_MS);

          pendingAction_ = ACTION_NONE;
          currentScreen_ = SettingsScreen::Cleanup;
          cleanupView_.needsRender = true;
          needsRender_ = true;

        } else if (pendingAction_ == ACTION_CLEAR_RECENT) {
          const bool ok = RecentBooksStore::instance().clearAndSave();
          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, ok ? tr(DONE) : tr(ERROR));
          vTaskDelay(1000 / portTICK_PERIOD_MS);

          pendingAction_ = ACTION_NONE;
          currentScreen_ = SettingsScreen::Cleanup;
          cleanupView_.needsRender = true;
          needsRender_ = true;

        } else if (pendingAction_ == ACTION_EMPTY_TRASH) {
          const auto exists = core.storage.exists(trash::DIRECTORY);
          if (!exists.ok() || !*exists) {
            ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(NO_TRASH_TO_EMPTY));
          } else {
            ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(EMPTYING_TRASH));
            auto result = core.storage.rmdir(trash::DIRECTORY);
            if (result.ok()) {
              result = core.storage.mkdir(trash::DIRECTORY);
            }
            ui::centeredMessage(renderer_, THEME, THEME.uiFontId, result.ok() ? tr(TRASH_EMPTIED) : tr(DELETE_FAILED));
          }
          vTaskDelay(1500 / portTICK_PERIOD_MS);

          pendingAction_ = ACTION_NONE;
          currentScreen_ = SettingsScreen::Cleanup;
          cleanupView_.needsRender = true;
          needsRender_ = true;

        } else if (pendingAction_ == ACTION_CLEAR_DEVICE_STORAGE) {
          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(CLEARING_STORAGE));

          LittleFS.format();

          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(DONE_RESTARTING));
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          ESP.restart();

        } else if (pendingAction_ == ACTION_FACTORY_RESET) {
          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(RESETTING_DEVICE));

          LittleFS.format();
          core.storage.rmdir(PAPYRIX_DIR);

          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(DONE_RESTARTING));
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          ESP.restart();
        }
      } else {
        // No - cancel
        pendingAction_ = ACTION_NONE;
        currentScreen_ = SettingsScreen::Cleanup;
        cleanupView_.needsRender = true;
        needsRender_ = true;
      }
      break;

    case SettingsScreen::FirmwareUpdate:
      if (firmwareView_.state == ui::FirmwareUpdateView::State::Error) {
        firmwareView_.state = ui::FirmwareUpdateView::State::Idle;
        firmwareView_.needsRender = true;
        needsRender_ = true;
        FW_UPDATER.reset();
        if (FW_UPDATER.findFirmwareFile()) {
          snprintf(firmwareView_.statusLine, sizeof(firmwareView_.statusLine), "%s: %s", tr(FIRMWARE_FILE_FOUND),
                   PAPYRIX_FIRMWARE_FILE);
        } else {
          snprintf(firmwareView_.statusLine, sizeof(firmwareView_.statusLine), "%s", tr(NO_FIRMWARE_FILE));
        }
      } else if (firmwareView_.state == ui::FirmwareUpdateView::State::Idle) {
        if (!FW_UPDATER.isFirmwareAvailable()) {
          firmwareView_.state = ui::FirmwareUpdateView::State::Error;
          snprintf(firmwareView_.statusLine, sizeof(firmwareView_.statusLine), "%s", tr(NO_FIRMWARE_FILE));
          firmwareView_.needsRender = true;
          needsRender_ = true;
          break;
        }
        firmwareView_.state = ui::FirmwareUpdateView::State::Validating;
        firmwareValidationRendered_ = false;
        snprintf(firmwareView_.statusLine, sizeof(firmwareView_.statusLine), "%s", tr(VALIDATING_FIRMWARE));
        firmwareView_.progressPercent = 0;
        firmwareView_.needsRender = true;
        needsRender_ = true;
      }
      break;
  }
}

void SettingsState::handleLeftRight(int delta) {
  if (currentScreen_ == SettingsScreen::Screen && screenView_.lightSelected()) {
    const uint8_t value = screenView_.adjustedLightValue(delta);
    const bool brightness = screenView_.settingIndex(screenView_.selected) == 1;
    const uint8_t previous = brightness ? core_->frontLight.brightness() : core_->frontLight.warmth();
    if (value == previous) return;
    const bool saved = brightness ? core_->frontLight.setBrightness(value) : core_->frontLight.setWarmth(value);
    screenView_.values[1] = core_->frontLight.brightness();
    screenView_.values[2] = core_->frontLight.warmth();
    if (!saved) LOG_ERR(TAG, "Could not save front light setting");
    needsRender_ = true;
    return;
  }
  if (currentScreen_ == SettingsScreen::Reader) {
    readerView_.cycleValue(delta);
    saveReaderSettings();
    needsRender_ = true;
  } else if (currentScreen_ == SettingsScreen::Screen) {
    screenView_.cycleValue(delta);
    saveScreenSettings();
    needsRender_ = true;
  } else if (currentScreen_ == SettingsScreen::Device) {
    deviceView_.cycleValue(delta);
    saveDeviceSettings();
    needsRender_ = true;
  }
}

void SettingsState::loadReaderSettings() {
  const auto& settings = core_->settings;
  readerView_.values[0] = settings.fontSize;
  readerView_.values[1] = settings.textLayout;
  readerView_.values[2] = settings.lineSpacing;
  readerView_.values[3] = settings.paragraphAlignment;
  readerView_.values[4] = settings.hyphenation;
  readerView_.values[5] = settings.showImages;
  readerView_.values[6] = settings.statusBar;
  readerView_.values[7] = settings.touchPageTurns;
  readerView_.values[8] = settings.fullBookProcess;
  readerView_.visibleCount =
      ui::ReaderSettingsView::SETTING_COUNT - (board::hasTouch(board::HardwareIdentity::instance().profile()) ? 0 : 1);
}

void SettingsState::saveReaderSettings() {
  auto& settings = core_->settings;
  settings.fontSize = readerView_.values[0];
  settings.textLayout = readerView_.values[1];
  settings.lineSpacing = readerView_.values[2];
  settings.paragraphAlignment = readerView_.values[3];
  settings.hyphenation = readerView_.values[4];
  settings.showImages = readerView_.values[5];
  settings.statusBar = readerView_.values[6];
  if (readerView_.visibleCount == ui::ReaderSettingsView::SETTING_COUNT) {
    settings.touchPageTurns = readerView_.values[7];
  }
  settings.fullBookProcess = readerView_.values[8];
}

void SettingsState::loadScreenSettings() {
  const auto& settings = core_->settings;
  auto themes = THEME_MANAGER.listAvailableThemes();
  screenView_.themeCount = 0;
  screenView_.currentThemeIndex = 0;
  for (size_t i = 0; i < themes.size() && i < ui::ScreenSettingsView::MAX_THEMES; i++) {
    strncpy(screenView_.themeNames[i], themes[i].c_str(), sizeof(screenView_.themeNames[i]) - 1);
    screenView_.themeNames[i][sizeof(screenView_.themeNames[i]) - 1] = '\0';
    if (themes[i] == settings.themeName) screenView_.currentThemeIndex = static_cast<int>(i);
    screenView_.themeCount++;
  }
  screenView_.visibleCount = ui::ScreenSettingsView::SETTING_COUNT - (core_->frontLight.isAvailable() ? 0 : 2);
  screenView_.values[1] = core_->frontLight.brightness();
  screenView_.values[2] = core_->frontLight.warmth();
  screenView_.values[3] = settings.orientation;
  screenView_.values[4] = settings.textAntiAliasing;
  screenView_.values[5] = settings.pagesPerRefresh;
  screenView_.values[6] = settings.sunlightFadingFix;
  screenView_.values[7] = settings.sleepScreen;
}

void SettingsState::saveScreenSettings() {
  auto& settings = core_->settings;
  const char* selectedTheme = screenView_.getCurrentThemeName();
  if (strcmp(settings.themeName, selectedTheme) != 0) {
    strncpy(settings.themeName, selectedTheme, sizeof(settings.themeName) - 1);
    settings.themeName[sizeof(settings.themeName) - 1] = '\0';
    if (!THEME_MANAGER.applyCachedTheme(settings.themeName)) {
      THEME_MANAGER.loadTheme(settings.themeName);
    }
    themeWasChanged_ = true;
  }
  settings.orientation = screenView_.values[3];
  settings.textAntiAliasing = screenView_.values[4];
  settings.pagesPerRefresh = screenView_.values[5];
  settings.sunlightFadingFix = screenView_.values[6];
  settings.sleepScreen = screenView_.values[7];
}

void SettingsState::loadDeviceSettings() {
  const auto& settings = core_->settings;
  deviceView_.values[0] = settings.frontButtonLayout;
  deviceView_.values[1] = settings.sideButtonLayout;
  deviceView_.values[2] = settings.shortPwrBtn;
  deviceView_.values[3] = settings.startupBehavior;
  deviceView_.values[4] = settings.showRecents;
  deviceView_.values[5] = settings.autoSleepMinutes;
  deviceView_.values[6] = settings.recycleBinEnabled;
}

void SettingsState::saveDeviceSettings() {
  auto& settings = core_->settings;
  settings.shortPwrBtn = deviceView_.values[2];
  settings.startupBehavior = deviceView_.values[3];
  settings.showRecents = deviceView_.values[4];
  settings.autoSleepMinutes = deviceView_.values[5];
  settings.recycleBinEnabled = deviceView_.values[6];
  // Apply button layouts in goBack() to prevent remapping the active press.
}

void SettingsState::populateSystemInfo() {
  infoView_.clear();

  // Firmware version
  infoView_.setField(ui::SystemInfoView::Field::Version, tr(VERSION), PAPYRIX_VERSION);

  // Uptime
  const unsigned long uptimeSeconds = millis() / 1000;
  const unsigned long hours = uptimeSeconds / 3600;
  const unsigned long minutes = (uptimeSeconds % 3600) / 60;
  const unsigned long seconds = uptimeSeconds % 60;
  char uptimeStr[24];
  snprintf(uptimeStr, sizeof(uptimeStr), "%luh %lum %lus", hours, minutes, seconds);
  infoView_.setField(ui::SystemInfoView::Field::Uptime, tr(UPTIME), uptimeStr);

  const auto batteryStatus = core.battery.readStatus();
  char batteryStr[24];
  if (!batteryStatus.percentageKnown || !batteryStatus.millivoltsKnown) {
    snprintf(batteryStr, sizeof(batteryStr), "-- (--mV)");
  } else {
    snprintf(batteryStr, sizeof(batteryStr), "%u%% (%umV)", batteryStatus.percentage, batteryStatus.millivolts);
  }
  infoView_.setField(ui::SystemInfoView::Field::Battery, tr(BATTERY), batteryStr);

  // Chip model
  infoView_.setField(ui::SystemInfoView::Field::Chip, tr(CHIP), ESP.getChipModel());

  // CPU frequency
  char freqStr[16];
  snprintf(freqStr, sizeof(freqStr), "%d MHz", ESP.getCpuFreqMHz());
  infoView_.setField(ui::SystemInfoView::Field::Cpu, tr(CPU), freqStr);

  // Free heap memory
  char heapStr[24];
  snprintf(heapStr, sizeof(heapStr), "%lu KB", ESP.getFreeHeap() / 1024);
  infoView_.setField(ui::SystemInfoView::Field::FreeMemory, tr(FREE_MEMORY), heapStr);

  // Internal flash storage (LittleFS)
  const size_t totalBytes = LittleFS.totalBytes();
  const size_t usedBytes = LittleFS.usedBytes();
  char internalStr[32];
  snprintf(internalStr, sizeof(internalStr), "%lu / %lu KB", (unsigned long)(usedBytes / 1024),
           (unsigned long)(totalBytes / 1024));
  infoView_.setField(ui::SystemInfoView::Field::InternalDisk, tr(INTERNAL_DISK), internalStr);

  // SD Card status
  infoView_.setField(ui::SystemInfoView::Field::SdCard, tr(SD_CARD), SdMan.ready() ? tr(READY) : tr(NOT_AVAILABLE));
}

void SettingsState::clearCache(ui::CleanupMenuView::Item type) {
  switch (type) {
    case ui::CleanupMenuView::Item::ClearBookCache:
      confirmView_.setup(tr(CLEAR_CACHES_Q), tr(CLEAR_CACHES_MSG1), tr(CLEAR_CACHES_MSG2));
      pendingAction_ = ACTION_CLEAR_BOOK_CACHE;
      break;
    case ui::CleanupMenuView::Item::ClearRecent:
      confirmView_.setup(tr(CLEAR_RECENT_Q), tr(CLEAR_RECENT_MSG), nullptr);
      pendingAction_ = ACTION_CLEAR_RECENT;
      break;
    case ui::CleanupMenuView::Item::EmptyTrash:
      confirmView_.setup(tr(EMPTY_TRASH_Q), tr(EMPTY_TRASH_MSG1), tr(EMPTY_TRASH_MSG2));
      pendingAction_ = ACTION_EMPTY_TRASH;
      break;
    case ui::CleanupMenuView::Item::ClearDeviceStorage:
      confirmView_.setup(tr(CLEAR_DEVICE_Q), tr(CLEAR_DEVICE_MSG1), tr(CLEAR_DEVICE_MSG2));
      pendingAction_ = ACTION_CLEAR_DEVICE_STORAGE;
      break;
    case ui::CleanupMenuView::Item::FactoryReset:
      confirmView_.setup(tr(FACTORY_RESET_Q), tr(FACTORY_RESET_MSG1), tr(FACTORY_RESET_MSG2));
      pendingAction_ = ACTION_FACTORY_RESET;
      break;
    case ui::CleanupMenuView::Item::Count:
      return;
  }
  currentScreen_ = SettingsScreen::ConfirmDialog;
  needsRender_ = true;
}

}  // namespace papyrix
