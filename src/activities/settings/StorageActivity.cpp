#include "StorageActivity.h"

#include <Esp.h>
#include <GfxRenderer.h>
#include <LittleFS.h>

#include "CacheManager.h"
#include "ConfirmActionActivity.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"

namespace {
struct MenuItem {
  const char* name;
  const char* confirmTitle;
  const char* confirmLine1;
  const char* confirmLine2;
};

constexpr MenuItem menuItems[] = {
    {"Clear Book Caches", "Clear Caches?", "This will delete all book caches", "and reading progress."},
    {"Clear Device Storage", "Clear Device?", "This will erase internal flash", "storage. Device will restart."},
    {"Factory Reset", "Factory Reset?", "This will erase ALL data including", "settings and WiFi credentials!"},
};
}  // namespace

void StorageActivity::onEnter() {
  Activity::onEnter();
  render();
}

void StorageActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (selectedIndex > 0) {
      selectedIndex--;
      render();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (selectedIndex < MENU_COUNT - 1) {
      selectedIndex++;
      render();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    executeAction(selectedIndex);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onComplete();
    return;
  }
}

void StorageActivity::executeAction(int actionIndex) {
  if (actionIndex < 0 || actionIndex >= MENU_COUNT) return;
  const auto& item = menuItems[actionIndex];

  auto onConfirm = [this, actionIndex]() {
    // Do all work FIRST while ConfirmActionActivity is still alive

    if (actionIndex == 0) {
      // Clear Book Caches
      renderer.clearScreen(THEME.backgroundColor);
      renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, "Clearing caches...",
                                THEME.primaryTextBlack);
      renderer.displayBuffer();

      const int result = CacheManager::clearAllBookCaches();

      renderer.clearScreen(THEME.backgroundColor);
      char msg[64];
      if (result < 0) {
        snprintf(msg, sizeof(msg), "Failed to clear cache");
      } else if (result == 0) {
        snprintf(msg, sizeof(msg), "No caches to clear");
      } else if (result == 1) {
        snprintf(msg, sizeof(msg), "Cleared 1 book cache");
      } else {
        snprintf(msg, sizeof(msg), "Cleared %d book caches", result);
      }
      renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, msg, THEME.primaryTextBlack);
      renderer.displayBuffer();
      vTaskDelay(1500 / portTICK_PERIOD_MS);

    } else if (actionIndex == 1) {
      // Clear Device Storage - format LittleFS and restart
      renderer.clearScreen(THEME.backgroundColor);
      renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, "Clearing device storage...",
                                THEME.primaryTextBlack);
      renderer.displayBuffer();

      LittleFS.format();

      renderer.clearScreen(THEME.backgroundColor);
      renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, "Done. Restarting...",
                                THEME.primaryTextBlack);
      renderer.displayBuffer();
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      ESP.restart();
      return;

    } else if (actionIndex == 2) {
      // Factory Reset - does not return (restarts device)
      renderer.clearScreen(THEME.backgroundColor);
      renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, "Resetting device...",
                                THEME.primaryTextBlack);
      renderer.displayBuffer();

      CacheManager::factoryReset();
      return;  // factoryReset() calls ESP.restart() - never reached, but clarifies intent
    }

    // Exit AFTER all work is done to avoid use-after-free
    exitActivity();
    render();
  };

  auto onCancel = [this]() {
    exitActivity();
    render();
  };

  enterNewActivity(new ConfirmActionActivity(renderer, mappedInput, item.confirmTitle, item.confirmLine1,
                                             item.confirmLine2, std::move(onConfirm), std::move(onCancel)));
}

void StorageActivity::render() const {
  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen(THEME.backgroundColor);

  // Header
  renderer.drawCenteredText(THEME.readerFontId, 10, "Cleanup", THEME.primaryTextBlack, BOLD);

  // Menu items - start at Y=60 like SettingsActivity
  for (int i = 0; i < MENU_COUNT; i++) {
    const int itemY = 60 + i * THEME.itemHeight;
    const bool isSelected = (i == selectedIndex);

    if (isSelected) {
      renderer.fillRect(0, itemY - 2, pageWidth - 1, THEME.itemHeight, THEME.selectionFillBlack);
    }

    const bool textColor = isSelected ? THEME.selectionTextBlack : THEME.primaryTextBlack;

    if (isSelected) {
      renderer.drawText(THEME.uiFontId, 5, itemY, ">", textColor);
    }

    renderer.drawText(THEME.uiFontId, 20, itemY, menuItems[i].name, textColor);
  }

  // Button hints
  const auto btnLabels = mappedInput.mapLabels("Back", "Select", "", "");
  renderer.drawButtonHints(THEME.uiFontId, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4,
                           THEME.primaryTextBlack);

  renderer.displayBuffer();
}
