#include "SettingsState.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <LittleFS.h>  // Must be before SdFat includes to avoid FILE_READ/FILE_WRITE redefinition
#include <Logging.h>
#include <SDCardManager.h>

#include <algorithm>

#include "../Battery.h"

#define TAG "SETTINGS_UI"
#include "../config.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"

namespace papyrix {

SettingsState::SettingsState(GfxRenderer& renderer)
    : renderer_(renderer),
      core_(nullptr),
      currentScreen_(SettingsScreen::Menu),
      needsRender_(true),
      goHome_(false),
      goNetwork_(false),
      themeWasChanged_(false),
      returnScreen_(SettingsScreen::Menu),
      pendingAction_(0),
      menuView_{},
      readerView_{},
      deviceView_{},
      cleanupView_{},
      confirmView_{},
      infoView_{} {}

SettingsState::~SettingsState() = default;

void SettingsState::enter(Core& core) {
  LOG_INF(TAG, "Entering");
  core_ = &core;  // Store for helper methods
  currentScreen_ = returnScreen_;
  returnScreen_ = SettingsScreen::Menu;  // Reset for next normal entry

  ui::ReaderSettingsView::initDefs();
  ui::DeviceSettingsView::initDefs();

  // Reset all views to ensure clean state
  menuView_.selected = 0;
  menuView_.needsRender = true;
  readerView_.selected = 0;
  readerView_.needsRender = true;
  deviceView_.selected = 0;
  deviceView_.needsRender = true;
  cleanupView_.selected = 0;
  cleanupView_.needsRender = true;
  confirmView_.needsRender = true;
  infoView_.clear();
  infoView_.needsRender = true;

  needsRender_ = true;
  goHome_ = false;
  goNetwork_ = false;
  themeWasChanged_ = false;
  pendingAction_ = 0;
}

void SettingsState::exit(Core& core) {
  LOG_INF(TAG, "Exiting");
  // Save settings on exit
  core.settings.save(core.storage);
}

StateTransition SettingsState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
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
              pendingAction_ = 0;
              currentScreen_ = SettingsScreen::Cleanup;
              cleanupView_.needsRender = true;
              needsRender_ = true;
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
  }

  needsRender_ = false;
  core.display.markDirty();
}

void SettingsState::openSelected() {
  switch (menuView_.selected) {
    case 0:  // Reader
      loadReaderSettings();
      readerView_.selected = 0;
      readerView_.needsRender = true;
      currentScreen_ = SettingsScreen::Reader;
      break;
    case 1:  // Device
      loadDeviceSettings();
      deviceView_.selected = 0;
      deviceView_.needsRender = true;
      currentScreen_ = SettingsScreen::Device;
      break;
    case 2:  // Cleanup
      cleanupView_.selected = 0;
      cleanupView_.needsRender = true;
      currentScreen_ = SettingsScreen::Cleanup;
      break;
    case 3:  // System Info
      populateSystemInfo();
      infoView_.needsRender = true;
      currentScreen_ = SettingsScreen::SystemInfo;
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
    case SettingsScreen::Device:
      saveDeviceSettings();
      // Apply button layouts now that we're leaving the screen
      core.settings.frontButtonLayout = std::min(deviceView_.values[6], uint8_t(Settings::FrontLRBC));
      core.settings.sideButtonLayout = std::min(deviceView_.values[7], uint8_t(Settings::NextPrev));
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
      pendingAction_ = 0;
      currentScreen_ = SettingsScreen::Cleanup;
      cleanupView_.needsRender = true;
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
      deviceView_.cycleValue(1);
      saveDeviceSettings();
      needsRender_ = true;
      break;

    case SettingsScreen::Cleanup:
      clearCache(cleanupView_.selected, core);
      break;

    case SettingsScreen::SystemInfo:
      goBack(core);
      break;

    case SettingsScreen::ConfirmDialog:
      if (confirmView_.isYesSelected()) {
        if (pendingAction_ == 10) {
          // Clear Book Cache
          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(CLEARING_CACHE));

          auto result = core.storage.rmdir(PAPYRIX_CACHE_DIR);

          const char* msg = result.ok() ? tr(CACHE_CLEARED) : tr(NO_CACHE_TO_CLEAR);
          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, msg);
          vTaskDelay(1500 / portTICK_PERIOD_MS);

          pendingAction_ = 0;
          currentScreen_ = SettingsScreen::Cleanup;
          cleanupView_.needsRender = true;
          needsRender_ = true;

        } else if (pendingAction_ == 11) {
          // Clear Device Storage
          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(CLEARING_STORAGE));

          LittleFS.format();

          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(DONE_RESTARTING));
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          ESP.restart();

        } else if (pendingAction_ == 12) {
          // Factory Reset
          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(RESETTING_DEVICE));

          LittleFS.format();
          core.storage.rmdir(PAPYRIX_DIR);

          ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(DONE_RESTARTING));
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          ESP.restart();
        }
      } else {
        // No - cancel
        pendingAction_ = 0;
        currentScreen_ = SettingsScreen::Cleanup;
        cleanupView_.needsRender = true;
        needsRender_ = true;
      }
      break;
  }
}

void SettingsState::handleLeftRight(int delta) {
  if (currentScreen_ == SettingsScreen::Reader) {
    readerView_.cycleValue(delta);
    saveReaderSettings();
    needsRender_ = true;
  } else if (currentScreen_ == SettingsScreen::Device) {
    deviceView_.cycleValue(delta);
    saveDeviceSettings();
    needsRender_ = true;
  }
}

void SettingsState::loadReaderSettings() {
  auto& settings = core_->settings;

  // Index 0: Theme (ThemeSelect) - load available themes from SD card
  auto themes = THEME_MANAGER.listAvailableThemes();
  readerView_.themeCount = 0;
  readerView_.currentThemeIndex = 0;
  for (size_t i = 0; i < themes.size() && i < ui::ReaderSettingsView::MAX_THEMES; i++) {
    strncpy(readerView_.themeNames[i], themes[i].c_str(), sizeof(readerView_.themeNames[i]) - 1);
    readerView_.themeNames[i][sizeof(readerView_.themeNames[i]) - 1] = '\0';
    if (themes[i] == settings.themeName) {
      readerView_.currentThemeIndex = static_cast<int>(i);
    }
    readerView_.themeCount++;
  }
  readerView_.values[0] = 0;  // Not used for ThemeSelect

  // Index 1: Font Size (0=Small, 1=Normal, 2=Large)
  readerView_.values[1] = settings.fontSize;

  // Index 2: Text Layout (0=Compact, 1=Standard, 2=Large)
  readerView_.values[2] = settings.textLayout;

  // Index 3: Line Spacing (0=Compact, 1=Normal, 2=Relaxed, 3=Large)
  readerView_.values[3] = settings.lineSpacing;

  // Index 4: Text Anti-Aliasing (toggle)
  readerView_.values[4] = settings.textAntiAliasing;

  // Index 5: Paragraph Alignment (0=Justified, 1=Left, 2=Center, 3=Right)
  readerView_.values[5] = settings.paragraphAlignment;

  // Index 6: Hyphenation (toggle)
  readerView_.values[6] = settings.hyphenation;

  // Index 7: Show Images (toggle)
  readerView_.values[7] = settings.showImages;

  // Index 8: Status Bar (0=None, 1=Title, 2=Chapter)
  readerView_.values[8] = settings.statusBar;

  // Index 9: Reading Orientation (0=Portrait, 1=Landscape CW, 2=Inverted, 3=Landscape CCW)
  readerView_.values[9] = settings.orientation;
}

void SettingsState::saveReaderSettings() {
  auto& settings = core_->settings;

  // Index 0: Theme (ThemeSelect) - apply selected theme
  const char* selectedTheme = readerView_.getCurrentThemeName();
  if (strcmp(settings.themeName, selectedTheme) != 0) {
    strncpy(settings.themeName, selectedTheme, sizeof(settings.themeName) - 1);
    settings.themeName[sizeof(settings.themeName) - 1] = '\0';
    // Use cached theme for instant switching (no file I/O)
    if (!THEME_MANAGER.applyCachedTheme(settings.themeName)) {
      THEME_MANAGER.loadTheme(settings.themeName);
    }
    themeWasChanged_ = true;
  }

  // Index 1: Font Size
  settings.fontSize = readerView_.values[1];

  // Index 2: Text Layout
  settings.textLayout = readerView_.values[2];

  // Index 3: Line Spacing
  settings.lineSpacing = readerView_.values[3];

  // Index 4: Text Anti-Aliasing
  settings.textAntiAliasing = readerView_.values[4];

  // Index 5: Paragraph Alignment
  settings.paragraphAlignment = readerView_.values[5];

  // Index 6: Hyphenation
  settings.hyphenation = readerView_.values[6];

  // Index 7: Show Images
  settings.showImages = readerView_.values[7];

  // Index 8: Status Bar
  settings.statusBar = readerView_.values[8];

  // Index 9: Reading Orientation
  settings.orientation = readerView_.values[9];
}

void SettingsState::loadDeviceSettings() {
  const auto& settings = core_->settings;

  // Index 0: Auto Sleep Timeout (5 min=0, 10 min=1, 15 min=2, 30 min=3, Never=4)
  deviceView_.values[0] = settings.autoSleepMinutes;

  // Index 1: Sleep Screen (Dark=0, Light=1, Custom=2, Cover=3)
  deviceView_.values[1] = settings.sleepScreen;

  // Index 2: Startup Behavior (Last Document=0, Home=1)
  deviceView_.values[2] = settings.startupBehavior;

  // Index 3: Short Power Button (Ignore=0, Sleep=1, Page Turn=2)
  deviceView_.values[3] = settings.shortPwrBtn;

  // Index 4: Pages Per Refresh (1=0, 5=1, 10=2, 15=3, 30=4)
  deviceView_.values[4] = settings.pagesPerRefresh;

  // Index 5: Sunlight Fading Fix (toggle)
  deviceView_.values[5] = settings.sunlightFadingFix;

  // Index 6: Front Buttons (B/C/L/R=0, L/R/B/C=1)
  deviceView_.values[6] = settings.frontButtonLayout;

  // Index 7: Side Buttons (Prev/Next=0, Next/Prev=1)
  deviceView_.values[7] = settings.sideButtonLayout;
}

void SettingsState::saveDeviceSettings() {
  auto& settings = core_->settings;

  // Index 0: Auto Sleep Timeout
  settings.autoSleepMinutes = deviceView_.values[0];

  // Index 1: Sleep Screen
  settings.sleepScreen = deviceView_.values[1];

  // Index 2: Startup Behavior
  settings.startupBehavior = deviceView_.values[2];

  // Index 3: Short Power Button
  settings.shortPwrBtn = deviceView_.values[3];

  // Index 4: Pages Per Refresh
  settings.pagesPerRefresh = deviceView_.values[4];

  // Index 5: Sunlight Fading Fix
  settings.sunlightFadingFix = deviceView_.values[5];

  // Index 6: Front Buttons - deferred to goBack() on screen exit.
  // Changing layout while navigating causes ghost button events because the
  // MappedInputManager remaps physical buttons mid-press.

  // Index 7: Side Buttons - deferred to goBack() on screen exit.
  // Same as front buttons: changing layout mid-navigation causes ghost events.
}

void SettingsState::populateSystemInfo() {
  infoView_.clear();

  // Firmware version
  infoView_.addField(tr(VERSION), PAPYRIX_VERSION);

  // Uptime
  const unsigned long uptimeSeconds = millis() / 1000;
  const unsigned long hours = uptimeSeconds / 3600;
  const unsigned long minutes = (uptimeSeconds % 3600) / 60;
  const unsigned long seconds = uptimeSeconds % 60;
  char uptimeStr[24];
  snprintf(uptimeStr, sizeof(uptimeStr), "%luh %lum %lus", hours, minutes, seconds);
  infoView_.addField(tr(UPTIME), uptimeStr);

  // Battery
  const uint16_t millivolts = batteryMonitor.readMillivolts();
  const uint16_t percentage = batteryMonitor.readPercentage();
  char batteryStr[24];
  if (millivolts < 100) {
    snprintf(batteryStr, sizeof(batteryStr), "-- (--mV)");
  } else {
    snprintf(batteryStr, sizeof(batteryStr), "%u%% (%umV)", percentage, millivolts);
  }
  infoView_.addField(tr(BATTERY), batteryStr);

  // Chip model
  infoView_.addField(tr(CHIP), ESP.getChipModel());

  // CPU frequency
  char freqStr[16];
  snprintf(freqStr, sizeof(freqStr), "%d MHz", ESP.getCpuFreqMHz());
  infoView_.addField(tr(CPU), freqStr);

  // Free heap memory
  char heapStr[24];
  snprintf(heapStr, sizeof(heapStr), "%lu KB", ESP.getFreeHeap() / 1024);
  infoView_.addField(tr(FREE_MEMORY), heapStr);

  // Internal flash storage (LittleFS)
  const size_t totalBytes = LittleFS.totalBytes();
  const size_t usedBytes = LittleFS.usedBytes();
  char internalStr[32];
  snprintf(internalStr, sizeof(internalStr), "%lu / %lu KB", (unsigned long)(usedBytes / 1024),
           (unsigned long)(totalBytes / 1024));
  infoView_.addField(tr(INTERNAL_DISK), internalStr);

  // SD Card status
  infoView_.addField(tr(SD_CARD), SdMan.ready() ? tr(READY) : tr(NOT_AVAILABLE));
}

void SettingsState::clearCache(int type, Core& core) {
  // Set up confirmation dialog messages based on action type
  if (type == 0) {
    // Clear Book Cache - show confirmation
    confirmView_.setup(tr(CLEAR_CACHES_Q), tr(CLEAR_CACHES_MSG1), tr(CLEAR_CACHES_MSG2));
    pendingAction_ = 10;
    currentScreen_ = SettingsScreen::ConfirmDialog;
    needsRender_ = true;
    return;
  } else if (type == 1) {
    // Clear Device Storage
    confirmView_.setup(tr(CLEAR_DEVICE_Q), tr(CLEAR_DEVICE_MSG1), tr(CLEAR_DEVICE_MSG2));
    pendingAction_ = 11;
    currentScreen_ = SettingsScreen::ConfirmDialog;
    needsRender_ = true;
    return;
  } else if (type == 2) {
    // Factory Reset
    confirmView_.setup(tr(FACTORY_RESET_Q), tr(FACTORY_RESET_MSG1), tr(FACTORY_RESET_MSG2));
    pendingAction_ = 12;
    currentScreen_ = SettingsScreen::ConfirmDialog;
    needsRender_ = true;
    return;
  }
}

}  // namespace papyrix
