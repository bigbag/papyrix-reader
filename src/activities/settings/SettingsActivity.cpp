#include "SettingsActivity.h"

#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "DeviceSettingsActivity.h"
#include "MappedInputManager.h"
#include "ReaderSettingsActivity.h"
#include "SystemInfoActivity.h"
#include "ThemeManager.h"
#include "ToolsSettingsActivity.h"
#include "config.h"

namespace {
constexpr const char* categoryNames[] = {"Reader", "Device", "Tools", "System Info"};
constexpr int categoryCount = 4;
}  // namespace

void SettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<SettingsActivity*>(param);
  self->displayTaskLoop();
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  selectedIndex = 0;
  updateRequired = true;

  xTaskCreate(&SettingsActivity::taskTrampoline, "SettingsActivityTask", 2048, this, 1, &displayTaskHandle);
}

void SettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

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

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openSelectedCategory();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    onGoHome();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex > 0) ? (selectedIndex - 1) : (categoryCount - 1);
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % categoryCount;
    updateRequired = true;
  }
}

void SettingsActivity::openSelectedCategory() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();

  auto onCategoryComplete = [this] {
    exitActivity();
    updateRequired = true;
  };

  switch (selectedIndex) {
    case 0:
      enterNewActivity(new ReaderSettingsActivity(renderer, mappedInput, onCategoryComplete));
      break;
    case 1:
      enterNewActivity(new DeviceSettingsActivity(renderer, mappedInput, onCategoryComplete));
      break;
    case 2:
      enterNewActivity(new ToolsSettingsActivity(renderer, mappedInput, onCategoryComplete, onOpdsLibraryOpen,
                                                 onCalibreWirelessOpen, onFileTransferOpen));
      break;
    case 3:
      enterNewActivity(new SystemInfoActivity(renderer, mappedInput, onCategoryComplete));
      break;
    default:
      break;
  }

  xSemaphoreGive(renderingMutex);
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

  renderer.drawCenteredText(THEME.readerFontId, 10, "Settings", THEME.primaryTextBlack, BOLD);

  renderer.fillRect(0, 60 + selectedIndex * THEME.itemHeight - 2, pageWidth - 1, THEME.itemHeight,
                    THEME.selectionFillBlack);

  for (int i = 0; i < categoryCount; i++) {
    const int itemY = 60 + i * THEME.itemHeight;
    const bool isSelected = (i == selectedIndex);
    const bool textColor = isSelected ? THEME.selectionTextBlack : THEME.primaryTextBlack;

    if (isSelected) {
      renderer.drawText(THEME.uiFontId, 5, itemY, ">", textColor);
    }

    renderer.drawText(THEME.uiFontId, 20, itemY, categoryNames[i], textColor);
  }

  renderer.drawText(THEME.smallFontId, pageWidth - 20 - renderer.getTextWidth(THEME.smallFontId, PAPYRIX_VERSION),
                    pageHeight - 60, PAPYRIX_VERSION, THEME.primaryTextBlack);

  const auto labels = mappedInput.mapLabels("Save", "Open", "", "");
  renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4, THEME.primaryTextBlack);

  renderer.displayBuffer();
}
