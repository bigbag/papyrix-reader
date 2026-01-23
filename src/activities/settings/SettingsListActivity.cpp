#include "SettingsListActivity.h"

#include <GfxRenderer.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "FontManager.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "config.h"

void SettingsListActivity::taskTrampoline(void* param) {
  auto* self = static_cast<SettingsListActivity*>(param);
  self->displayTaskLoop();
}

void SettingsListActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  selectedIndex = 0;

  // Load themes if any setting uses THEME_SELECT
  const auto* settings = getSettings();
  const int count = getSettingsCount();
  for (int i = 0; i < count; i++) {
    if (settings[i].type == SettingType::THEME_SELECT) {
      loadAvailableThemes();
      break;
    }
  }

  updateRequired = true;

  xTaskCreate(&SettingsListActivity::taskTrampoline, "SettingsListTask", 2048, this, 1, &displayTaskHandle);
}

void SettingsListActivity::loadAvailableThemes() {
  availableThemes = THEME_MANAGER.listAvailableThemes();

  currentThemeIndex = 0;
  const char* currentTheme = SETTINGS.themeName;
  for (size_t i = 0; i < availableThemes.size(); i++) {
    if (availableThemes[i] == currentTheme) {
      currentThemeIndex = static_cast<int>(i);
      break;
    }
  }
}

void SettingsListActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void SettingsListActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    updateRequired = true;
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    if (themeWasChanged) {
      FONT_MANAGER.unloadAllFonts();
      applyThemeFonts();
    }
    onComplete();
    return;
  }

  const int count = getSettingsCount();
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex > 0) ? (selectedIndex - 1) : (count - 1);
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % count;
    updateRequired = true;
  }
}

void SettingsListActivity::toggleCurrentSetting() {
  const int count = getSettingsCount();
  if (selectedIndex < 0 || selectedIndex >= count) {
    return;
  }

  const auto* settings = getSettings();
  const auto& setting = settings[selectedIndex];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr && setting.enumCount > 0) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % setting.enumCount;
    if (setting.valuePtr == &CrossPointSettings::fontSize) {
      FONT_MANAGER.unloadAllFonts();
      applyThemeFonts();
    }
  } else if (setting.type == SettingType::THEME_SELECT) {
    if (!availableThemes.empty()) {
      currentThemeIndex = (currentThemeIndex + 1) % static_cast<int>(availableThemes.size());
      const std::string& newTheme = availableThemes[currentThemeIndex];
      strncpy(SETTINGS.themeName, newTheme.c_str(), sizeof(SETTINGS.themeName) - 1);
      SETTINGS.themeName[sizeof(SETTINGS.themeName) - 1] = '\0';

      // Use cached theme for instant switching (no file I/O)
      // Font loading is deferred until settings screen is exited
      if (!THEME_MANAGER.applyCachedTheme(SETTINGS.themeName)) {
        THEME_MANAGER.loadTheme(SETTINGS.themeName);
      }
      themeWasChanged = true;
    }
  } else if (setting.type == SettingType::ACTION) {
    handleAction(setting.name);
    return;
  } else {
    return;
  }

  SETTINGS.saveToFile();
}

void SettingsListActivity::handleAction(const char* actionName) { (void)actionName; }

void SettingsListActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void SettingsListActivity::render() const {
  renderer.clearScreen(THEME.backgroundColor);

  const auto pageWidth = renderer.getScreenWidth();

  renderer.drawCenteredText(THEME.readerFontId, 10, title, THEME.primaryTextBlack, BOLD);

  const auto* settings = getSettings();
  const int count = getSettingsCount();

  renderer.fillRect(0, 60 + selectedIndex * THEME.itemHeight - 2, pageWidth - 1, THEME.itemHeight,
                    THEME.selectionFillBlack);

  for (int i = 0; i < count; i++) {
    const int settingY = 60 + i * THEME.itemHeight;
    const bool isSelected = (i == selectedIndex);
    const bool textColor = isSelected ? THEME.selectionTextBlack : THEME.primaryTextBlack;

    if (isSelected) {
      renderer.drawText(THEME.uiFontId, 5, settingY, ">", textColor);
    }

    renderer.drawText(THEME.uiFontId, 20, settingY, settings[i].name, textColor);

    std::string valueText;
    if (settings[i].type == SettingType::TOGGLE && settings[i].valuePtr != nullptr) {
      const bool value = SETTINGS.*(settings[i].valuePtr);
      valueText = value ? "ON" : "OFF";
    } else if (settings[i].type == SettingType::ENUM && settings[i].valuePtr != nullptr) {
      const uint8_t value = SETTINGS.*(settings[i].valuePtr);
      if (value < settings[i].enumCount) {
        valueText = settings[i].enumValues[value];
      }
    } else if (settings[i].type == SettingType::THEME_SELECT) {
      if (THEME.displayName[0] != '\0') {
        valueText = THEME.displayName;
      } else {
        valueText = SETTINGS.themeName;
      }
    }

    if (!valueText.empty()) {
      const auto width = renderer.getTextWidth(THEME.uiFontId, valueText.c_str());
      renderer.drawText(THEME.uiFontId, pageWidth - 20 - width, settingY, valueText.c_str(), textColor);
    }
  }

  const auto labels = mappedInput.mapLabels("Back", "Toggle", "", "");
  renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4, THEME.primaryTextBlack);

  renderer.displayBuffer();
}
