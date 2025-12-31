#include "SettingsActivity.h"

#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OtaUpdateActivity.h"
#include "ThemeManager.h"
#include "config.h"

// Define the static settings list
namespace {
// Enum value arrays (must match CrossPointSettings enums)
constexpr const char* sleepScreenValues[] = {"Dark", "Light", "Custom", "Cover"};
constexpr const char* statusBarValues[] = {"None", "No Progress", "Full"};
constexpr const char* fontSizeValues[] = {"Small", "Normal", "Large"};
constexpr const char* pagesPerRefreshValues[] = {"1", "5", "10", "15", "30"};
constexpr const char* orientationValues[] = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};
constexpr const char* sleepTimeoutValues[] = {"5 min", "10 min", "15 min", "30 min"};
constexpr const char* paragraphAlignmentValues[] = {"Justified", "Left", "Center", "Right"};

constexpr int settingsCount = 13;
const SettingInfo settingsList[settingsCount] = {
    // Theme
    {"Theme", SettingType::THEME_SELECT, nullptr, nullptr, 0},
    // Book Settings
    {"Font Size", SettingType::ENUM, &CrossPointSettings::fontSize, fontSizeValues, 3},
    {"Paragraph Alignment", SettingType::ENUM, &CrossPointSettings::paragraphAlignment, paragraphAlignmentValues, 4},
    {"Extra Paragraph Spacing", SettingType::TOGGLE, &CrossPointSettings::extraParagraphSpacing, nullptr, 0},
    {"Reading Orientation", SettingType::ENUM, &CrossPointSettings::orientation, orientationValues, 4},
    {"Status Bar", SettingType::ENUM, &CrossPointSettings::statusBar, statusBarValues, 3},
    {"Show Book Cover", SettingType::TOGGLE, &CrossPointSettings::showBookCover, nullptr, 0},
    {"Show Book Info", SettingType::TOGGLE, &CrossPointSettings::showBookMetadata, nullptr, 0},
    // Device Settings
    {"Pages Per Refresh", SettingType::ENUM, &CrossPointSettings::pagesPerRefresh, pagesPerRefreshValues, 5},
    {"Sleep Timeout", SettingType::ENUM, &CrossPointSettings::sleepTimeout, sleepTimeoutValues, 4},
    {"Sleep Screen", SettingType::ENUM, &CrossPointSettings::sleepScreen, sleepScreenValues, 4},
    {"Short Power Button Click", SettingType::TOGGLE, &CrossPointSettings::shortPwrBtn, nullptr, 0},
    {"Check for updates", SettingType::ACTION, nullptr, nullptr, 0},
};
}  // namespace

void SettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<SettingsActivity*>(param);
  self->displayTaskLoop();
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  // Reset selection to first item
  selectedSettingIndex = 0;

  // Load available themes
  loadAvailableThemes();

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&SettingsActivity::taskTrampoline, "SettingsActivityTask",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void SettingsActivity::loadAvailableThemes() {
  availableThemes = THEME_MANAGER.listAvailableThemes();

  // Find current theme index
  currentThemeIndex = 0;
  const char* currentTheme = SETTINGS.themeName;
  for (size_t i = 0; i < availableThemes.size(); i++) {
    if (availableThemes[i] == currentTheme) {
      currentThemeIndex = static_cast<int>(i);
      break;
    }
  }
}

void SettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void SettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    updateRequired = true;
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    onGoHome();
    return;
  }

  // Handle navigation
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    // Move selection up (with wrap-around)
    selectedSettingIndex = (selectedSettingIndex > 0) ? (selectedSettingIndex - 1) : (settingsCount - 1);
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    // Move selection down (with wrap-around)
    selectedSettingIndex = (selectedSettingIndex + 1) % settingsCount;
    updateRequired = true;
  }
}

void SettingsActivity::toggleCurrentSetting() {
  // Validate index
  if (selectedSettingIndex < 0 || selectedSettingIndex >= settingsCount) {
    return;
  }

  const auto& setting = settingsList[selectedSettingIndex];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % setting.enumCount;
  } else if (setting.type == SettingType::THEME_SELECT) {
    // Cycle through available themes
    if (!availableThemes.empty()) {
      currentThemeIndex = (currentThemeIndex + 1) % static_cast<int>(availableThemes.size());
      const std::string& newTheme = availableThemes[currentThemeIndex];
      strncpy(SETTINGS.themeName, newTheme.c_str(), sizeof(SETTINGS.themeName) - 1);
      SETTINGS.themeName[sizeof(SETTINGS.themeName) - 1] = '\0';
      // Apply the theme immediately
      THEME_MANAGER.loadTheme(SETTINGS.themeName);
    }
  } else if (setting.type == SettingType::ACTION) {
    if (std::string(setting.name) == "Check for updates") {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new OtaUpdateActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
      }));
      xSemaphoreGive(renderingMutex);
    }
  } else {
    // Only toggle if it's a toggle type and has a value pointer
    return;
  }

  // Save settings when they change
  SETTINGS.saveToFile();
}

void SettingsActivity::displayTaskLoop() {
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

void SettingsActivity::render() const {
  renderer.clearScreen(THEME.backgroundColor);

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Draw header
  renderer.drawCenteredText(THEME.readerFontId, 10, "Settings", THEME.primaryTextBlack, BOLD);

  // Draw selection highlight
  renderer.fillRect(0, 60 + selectedSettingIndex * THEME.itemHeight - 2, pageWidth - 1, THEME.itemHeight, THEME.selectionFillBlack);

  // Draw all settings
  for (int i = 0; i < settingsCount; i++) {
    const int settingY = 60 + i * THEME.itemHeight;
    const bool isSelected = (i == selectedSettingIndex);
    const bool textColor = isSelected ? THEME.selectionTextBlack : THEME.primaryTextBlack;

    // Draw selection indicator for the selected setting
    if (isSelected) {
      renderer.drawText(THEME.uiFontId, 5, settingY, ">", textColor);
    }

    // Draw setting name
    renderer.drawText(THEME.uiFontId, 20, settingY, settingsList[i].name, textColor);

    // Draw value based on setting type
    std::string valueText = "";
    if (settingsList[i].type == SettingType::TOGGLE && settingsList[i].valuePtr != nullptr) {
      const bool value = SETTINGS.*(settingsList[i].valuePtr);
      valueText = value ? "ON" : "OFF";
    } else if (settingsList[i].type == SettingType::ENUM && settingsList[i].valuePtr != nullptr) {
      const uint8_t value = SETTINGS.*(settingsList[i].valuePtr);
      valueText = settingsList[i].enumValues[value];
    } else if (settingsList[i].type == SettingType::THEME_SELECT) {
      // Show current theme display name (or filename if no display name set)
      if (THEME.displayName[0] != '\0') {
        valueText = THEME.displayName;
      } else {
        valueText = SETTINGS.themeName;
      }
    }
    const auto width = renderer.getTextWidth(THEME.uiFontId, valueText.c_str());
    renderer.drawText(THEME.uiFontId, pageWidth - 20 - width, settingY, valueText.c_str(), textColor);
  }

  // Draw version text above button hints
  renderer.drawText(THEME.smallFontId, pageWidth - 20 - renderer.getTextWidth(THEME.smallFontId, CROSSPOINT_VERSION),
                    pageHeight - 60, CROSSPOINT_VERSION, THEME.primaryTextBlack);

  // Draw help text
  const auto labels = mappedInput.mapLabels("« Save", "Toggle", "", "");
  renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4, THEME.primaryTextBlack);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
