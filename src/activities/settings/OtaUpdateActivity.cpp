#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "config.h"
#include "network/OtaUpdater.h"

void OtaUpdateActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OtaUpdateActivity*>(param);
  self->displayTaskLoop();
}

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  exitActivity();

  if (!success) {
    Serial.printf("[%lu] [OTA] WiFi connection failed, exiting\n", millis());
    goBack();
    return;
  }

  Serial.printf("[%lu] [OTA] WiFi connected, checking for update\n", millis());

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = CHECKING_FOR_UPDATE;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(10 / portTICK_PERIOD_MS);
  const auto res = updater.checkForUpdate();
  if (res != OtaUpdater::OK) {
    Serial.printf("[%lu] [OTA] Update check failed: %d\n", millis(), res);
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = FAILED;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  if (!updater.isUpdateNewer()) {
    Serial.printf("[%lu] [OTA] No new update available\n", millis());
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = NO_UPDATE;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = WAITING_CONFIRMATION;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void OtaUpdateActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  xTaskCreate(&OtaUpdateActivity::taskTrampoline, "OtaUpdateActivityTask",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );

  // Turn on WiFi immediately
  Serial.printf("[%lu] [OTA] Turning on WiFi...\n", millis());
  WiFi.mode(WIFI_STA);

  // Launch WiFi selection subactivity
  Serial.printf("[%lu] [OTA] Launching WifiSelectionActivity...\n", millis());
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void OtaUpdateActivity::onExit() {
  ActivityWithSubactivity::onExit();

  Serial.printf("[%lu] [OTA] [MEM] Free heap at onExit start: %d bytes\n", millis(), ESP.getFreeHeap());

  // Turn off wifi
  WiFi.disconnect(false);  // false = don't erase credentials, send disconnect frame
  delay(100);              // Allow disconnect frame to be sent
  WiFi.mode(WIFI_OFF);
  delay(100);  // Allow WiFi hardware to fully power down

  Serial.printf("[%lu] [OTA] [MEM] Free heap after WiFi off: %d bytes\n", millis(), ESP.getFreeHeap());

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  // WiFi fragments heap memory permanently on ESP32
  // Restart is required to read XTC books after using WiFi
  // WiFi is always started in onEnter(), so we always need to restart
  Serial.printf("[%lu] [OTA] Restarting to reclaim memory...\n", millis());
  ESP.restart();
}

void OtaUpdateActivity::displayTaskLoop() {
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

void OtaUpdateActivity::render() {
  if (subActivity) {
    // Subactivity handles its own rendering
    return;
  }

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS) {
    Serial.printf("[%lu] [OTA] Update progress: %d / %d\n", millis(), updater.processedSize, updater.totalSize);
    updaterProgress = static_cast<float>(updater.processedSize) / static_cast<float>(updater.totalSize);
    // Only update every 2% at the most
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
      return;
    }
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }

  const auto pageHeight = renderer.getScreenHeight();
  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen(THEME.backgroundColor);
  renderer.drawCenteredText(THEME.readerFontId, 10, "Update", THEME.primaryTextBlack, BOLD);

  if (state == CHECKING_FOR_UPDATE) {
    renderer.drawCenteredText(THEME.uiFontId, 300, "Checking for update...", THEME.primaryTextBlack, BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == WAITING_CONFIRMATION) {
    renderer.drawCenteredText(THEME.uiFontId, 200, "New update available!", THEME.primaryTextBlack, BOLD);
    renderer.drawText(THEME.uiFontId, 20, 250, "Current Version: " CROSSPOINT_VERSION, THEME.primaryTextBlack);
    renderer.drawText(THEME.uiFontId, 20, 270, ("New Version: " + updater.getLatestVersion()).c_str(),
                      THEME.primaryTextBlack);

    renderer.drawRect(25, pageHeight - 40, 106, 40, THEME.primaryTextBlack);
    renderer.drawText(THEME.uiFontId, 25 + (105 - renderer.getTextWidth(THEME.uiFontId, "Cancel")) / 2, pageHeight - 35,
                      "Cancel", THEME.primaryTextBlack);

    renderer.drawRect(130, pageHeight - 40, 106, 40, THEME.primaryTextBlack);
    renderer.drawText(THEME.uiFontId, 130 + (105 - renderer.getTextWidth(THEME.uiFontId, "Update")) / 2,
                      pageHeight - 35, "Update", THEME.primaryTextBlack);
    renderer.displayBuffer();
    return;
  }

  if (state == UPDATE_IN_PROGRESS) {
    renderer.drawCenteredText(THEME.uiFontId, 310, "Updating...", THEME.primaryTextBlack, BOLD);
    renderer.drawRect(20, 350, pageWidth - 40, 50, THEME.primaryTextBlack);
    renderer.fillRect(24, 354, static_cast<int>(updaterProgress * static_cast<float>(pageWidth - 44)), 42,
                      THEME.primaryTextBlack);
    char progressStr[8];
    snprintf(progressStr, sizeof(progressStr), "%d%%", static_cast<int>(updaterProgress * 100));
    renderer.drawCenteredText(THEME.uiFontId, 420, progressStr, THEME.primaryTextBlack);
    char sizeStr[32];
    snprintf(sizeStr, sizeof(sizeStr), "%zu / %zu", updater.processedSize, updater.totalSize);
    renderer.drawCenteredText(THEME.uiFontId, 440, sizeStr, THEME.primaryTextBlack);
    renderer.displayBuffer();
    return;
  }

  if (state == NO_UPDATE) {
    renderer.drawCenteredText(THEME.uiFontId, 300, "No update available", THEME.primaryTextBlack, BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(THEME.uiFontId, 300, "Update failed", THEME.primaryTextBlack, BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == FINISHED) {
    renderer.drawCenteredText(THEME.uiFontId, 300, "Update complete", THEME.primaryTextBlack, BOLD);
    renderer.drawCenteredText(THEME.uiFontId, 350, "Press and hold power button to turn back on",
                              THEME.primaryTextBlack);
    renderer.displayBuffer();
    state = SHUTTING_DOWN;
    return;
  }
}

void OtaUpdateActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == WAITING_CONFIRMATION) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      Serial.printf("[%lu] [OTA] New update available, starting download...\n", millis());
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = UPDATE_IN_PROGRESS;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);
      const auto res = updater.installUpdate([this](const size_t, const size_t) { updateRequired = true; });

      if (res != OtaUpdater::OK) {
        Serial.printf("[%lu] [OTA] Update failed: %d\n", millis(), res);
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = FAILED;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
        return;
      }

      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = FINISHED;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }

    return;
  }

  if (state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == NO_UPDATE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
