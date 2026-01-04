#include "OpdsServerListActivity.h"

#include <algorithm>

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "config.h"

void OpdsServerListActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OpdsServerListActivity*>(param);
  self->displayTaskLoop();
}

void OpdsServerListActivity::displayTaskLoop() {
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

void OpdsServerListActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  // Load servers - exactly like FileSelectionActivity::loadFiles()
  serverNames.clear();
  OPDS_STORE.loadFromFile();
  const auto& servers = OPDS_STORE.getServers();
  std::transform(servers.begin(), servers.end(), std::back_inserter(serverNames),
                 [](const OpdsServerConfig& server) { return server.name; });

  selectedIndex = 0;

  // Do first render synchronously to ensure selectedIndex=0 is used
  render();

  // Task will handle subsequent updates
  updateRequired = false;

  xTaskCreate(&OpdsServerListActivity::taskTrampoline, "OpdsListTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void OpdsServerListActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  serverNames.clear();
}

void OpdsServerListActivity::render() const {
  renderer.clearScreen(THEME.backgroundColor);

  const auto pageWidth = renderer.getScreenWidth();

  // Draw header (exactly like FileSelectionActivity)
  renderer.drawCenteredText(THEME.readerFontId, 10, "Net Library", THEME.primaryTextBlack, BOLD);

  // Help text (exactly like FileSelectionActivity - draw before list)
  const auto labels = mappedInput.mapLabels("Back", serverNames.empty() ? "" : "Connect", "", "");
  renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4, THEME.primaryTextBlack);

  if (serverNames.empty()) {
    renderer.drawText(THEME.uiFontId, 20, 60, "No servers configured", THEME.primaryTextBlack);
    renderer.displayBuffer();
    return;
  }

  // Draw selection highlight and list
  renderer.fillRect(0, 60 + selectedIndex * THEME.itemHeight - 2, pageWidth - 1, THEME.itemHeight, THEME.selectionFillBlack);
  for (size_t i = 0; i < serverNames.size(); i++) {
    const int itemY = 60 + static_cast<int>(i) * THEME.itemHeight;
    const bool isSelected = (static_cast<int>(i) == selectedIndex);
    const bool textColor = isSelected ? THEME.selectionTextBlack : THEME.primaryTextBlack;

    // Draw selection indicator (matching Settings view style)
    if (isSelected) {
      renderer.drawText(THEME.uiFontId, 5, itemY, ">", textColor);
    }

    renderer.drawText(THEME.uiFontId, 20, itemY, serverNames[i].c_str(), textColor);
  }

  renderer.displayBuffer();
}

void OpdsServerListActivity::loop() {
  // Navigation - exactly like SettingsActivity (with wrap-around, always triggers update)
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (!serverNames.empty()) {
      selectedIndex = (selectedIndex > 0) ? (selectedIndex - 1) : (static_cast<int>(serverNames.size()) - 1);
    }
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (!serverNames.empty()) {
      selectedIndex = (selectedIndex + 1) % static_cast<int>(serverNames.size());
    }
    updateRequired = true;
  }

  // Confirm - Select server
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (!serverNames.empty() && selectedIndex < static_cast<int>(serverNames.size())) {
      const auto& servers = OPDS_STORE.getServers();
      if (selectedIndex < static_cast<int>(servers.size())) {
        if (onServerSelected) {
          onServerSelected(servers[selectedIndex]);
        }
      }
    }
  }

  // Back - go back to settings
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onGoBack) {
      onGoBack();
    }
  }
}
