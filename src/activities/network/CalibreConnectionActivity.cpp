#include "CalibreConnectionActivity.h"

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "WifiSelectionActivity.h"
#include "calibre/CalibreSettings.h"
#include "config.h"

void CalibreConnectionActivity::taskTrampoline(void* param) {
  auto* self = static_cast<CalibreConnectionActivity*>(param);
  self->displayTaskLoop();
}

void CalibreConnectionActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  Serial.printf("[%lu] [CALACT] [MEM] Free heap at onEnter: %d bytes\n", millis(), ESP.getFreeHeap());

  renderingMutex = xSemaphoreCreateMutex();

  // Reset state
  state = CalibreActivityState::WIFI_SELECTION;
  connectedIP.clear();
  connectedSSID.clear();
  currentStatus = "Initializing...";
  currentBookTitle.clear();
  currentProgress = 0;
  currentTotal = 0;
  updateRequired = true;

  xTaskCreate(&CalibreConnectionActivity::taskTrampoline, "CalibreActivityTask",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );

  // Turn on WiFi and launch WiFi selection
  Serial.printf("[%lu] [CALACT] Turning on WiFi (STA mode)...\n", millis());
  WiFi.mode(WIFI_STA);

  Serial.printf("[%lu] [CALACT] Launching WifiSelectionActivity...\n", millis());
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void CalibreConnectionActivity::onExit() {
  ActivityWithSubactivity::onExit();

  Serial.printf("[%lu] [CALACT] [MEM] Free heap at onExit start: %d bytes\n", millis(), ESP.getFreeHeap());

  // Save state before modifying
  const auto stateBeforeExit = state;
  state = CalibreActivityState::SHUTTING_DOWN;

  // Stop Calibre server
  stopCalibreServer();

  // Stop mDNS
  MDNS.end();

  // CRITICAL: Wait for LWIP stack to flush pending packets
  Serial.printf("[%lu] [CALACT] Waiting 500ms for network stack to flush...\n", millis());
  delay(500);

  // Disconnect WiFi gracefully
  Serial.printf("[%lu] [CALACT] Disconnecting WiFi (graceful)...\n", millis());
  WiFi.disconnect(false);
  delay(100);

  Serial.printf("[%lu] [CALACT] Setting WiFi mode OFF...\n", millis());
  WiFi.mode(WIFI_OFF);
  delay(100);

  Serial.printf("[%lu] [CALACT] [MEM] Free heap after WiFi disconnect: %d bytes\n", millis(), ESP.getFreeHeap());

  // Acquire mutex before deleting task
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  Serial.printf("[%lu] [CALACT] [MEM] Free heap at onExit end: %d bytes\n", millis(), ESP.getFreeHeap());

  // Restart if WiFi was actually used (to reclaim fragmented heap)
  if (stateBeforeExit != CalibreActivityState::WIFI_SELECTION) {
    Serial.printf("[%lu] [CALACT] Restarting to reclaim memory...\n", millis());
    ESP.restart();
  }
}

void CalibreConnectionActivity::onWifiSelectionComplete(const bool connected) {
  Serial.printf("[%lu] [CALACT] WifiSelectionActivity completed, connected=%d\n", millis(), connected);

  if (connected) {
    // Get connection info
    connectedIP = static_cast<WifiSelectionActivity*>(subActivity.get())->getConnectedIP();
    connectedSSID = WiFi.SSID().c_str();

    exitActivity();

    // Start mDNS for hostname resolution
    if (MDNS.begin("papyrix-calibre")) {
      Serial.printf("[%lu] [CALACT] mDNS started: papyrix-calibre.local\n", millis());
    }

    // Start Calibre server
    startCalibreServer();
  } else {
    // User cancelled - go back
    exitActivity();
    onGoBack();
  }
}

void CalibreConnectionActivity::startCalibreServer() {
  Serial.printf("[%lu] [CALACT] Starting Calibre server...\n", millis());
  state = CalibreActivityState::STARTING_SERVER;
  updateRequired = true;

  calibreServer.reset(new CalibreDeviceServer());
  if (!calibreServer) {
    state = CalibreActivityState::ERROR;
    currentStatus = "Out of memory";
    updateRequired = true;
    return;
  }

  // Set callbacks
  calibreServer->setStatusCallback([this](const char* status) { onServerStatus(status); });
  calibreServer->setProgressCallback(
      [this](const char* title, size_t received, size_t total) { onServerProgress(title, received, total); });
  calibreServer->setBookReceivedCallback([this](const char* path) { onBookReceived(path); });
  calibreServer->setBookDeletedCallback([this](const char* path) { onBookDeleted(path); });

  if (calibreServer->begin()) {
    state = CalibreActivityState::WAITING;
    currentStatus = "Waiting for Calibre...";
    Serial.printf("[%lu] [CALACT] Calibre server started on port %u\n", millis(), calibreServer->getTcpPort());

    // Force immediate render
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    render();
    xSemaphoreGive(renderingMutex);
  } else {
    state = CalibreActivityState::ERROR;
    currentStatus = "Failed to start server";
    Serial.printf("[%lu] [CALACT] Failed to start Calibre server\n", millis());
    updateRequired = true;
  }
}

void CalibreConnectionActivity::stopCalibreServer() {
  if (calibreServer) {
    Serial.printf("[%lu] [CALACT] Stopping Calibre server...\n", millis());
    calibreServer->stop();
    calibreServer.reset();
  }
}

void CalibreConnectionActivity::onServerStatus(const char* status) {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  currentStatus = status;

  // Update state based on status
  if (calibreServer && calibreServer->isClientConnected()) {
    if (state == CalibreActivityState::WAITING) {
      state = CalibreActivityState::CONNECTED;
    }
  }

  updateRequired = true;
  xSemaphoreGive(renderingMutex);
}

void CalibreConnectionActivity::onServerProgress(const char* title, size_t received, size_t total) {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  currentBookTitle = title;
  currentProgress = received;
  currentTotal = total;

  state = CalibreActivityState::RECEIVING_BOOK;

  updateRequired = true;
  xSemaphoreGive(renderingMutex);
}

void CalibreConnectionActivity::onBookReceived(const char* path) {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  state = CalibreActivityState::TRANSFER_COMPLETE;
  currentStatus = "Book received!";

  updateRequired = true;
  xSemaphoreGive(renderingMutex);

  // Stay in complete state briefly, then go back to connected
  delay(1000);

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = CalibreActivityState::CONNECTED;
  currentStatus = "Connected to Calibre";
  currentBookTitle.clear();
  currentProgress = 0;
  currentTotal = 0;
  updateRequired = true;
  xSemaphoreGive(renderingMutex);
}

void CalibreConnectionActivity::onBookDeleted(const char* path) {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  currentStatus = "Book deleted";
  updateRequired = true;

  xSemaphoreGive(renderingMutex);
}

void CalibreConnectionActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Handle Calibre server
  if (calibreServer && calibreServer->isRunning()) {
    calibreServer->loop();
  }

  // Handle exit on Back button
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoBack();
    return;
  }
}

void CalibreConnectionActivity::displayTaskLoop() {
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

void CalibreConnectionActivity::render() const {
  renderer.clearScreen(THEME.backgroundColor);

  switch (state) {
    case CalibreActivityState::WAITING:
      renderWaiting();
      break;
    case CalibreActivityState::CONNECTED:
      renderConnected();
      break;
    case CalibreActivityState::RECEIVING_BOOK:
      renderReceiving();
      break;
    case CalibreActivityState::TRANSFER_COMPLETE:
      renderComplete();
      break;
    case CalibreActivityState::ERROR:
      renderError();
      break;
    case CalibreActivityState::STARTING_SERVER:
      renderer.drawCenteredText(THEME.readerFontId, renderer.getScreenHeight() / 2 - 20,
                                "Starting server...", THEME.primaryTextBlack, BOLD);
      break;
    default:
      break;
  }

  renderer.displayBuffer();
}

void CalibreConnectionActivity::renderWaiting() const {
  constexpr int LINE_SPACING = 32;
  const int startY = 40;

  renderer.drawCenteredText(THEME.readerFontId, startY, "Calibre Wireless", THEME.primaryTextBlack, BOLD);

  renderer.drawCenteredText(THEME.uiFontId, startY + LINE_SPACING * 2, "Waiting for Calibre...",
                            THEME.primaryTextBlack, REGULAR);

  // Show network info
  std::string networkInfo = "Network: " + connectedSSID;
  renderer.drawCenteredText(THEME.smallFontId, startY + LINE_SPACING * 4, networkInfo.c_str(),
                            THEME.primaryTextBlack, REGULAR);

  // Show IP and port
  uint16_t port = calibreServer ? calibreServer->getTcpPort() : 9090;
  char ipPort[64];
  snprintf(ipPort, sizeof(ipPort), "IP: %s:%u", connectedIP.c_str(), port);
  renderer.drawCenteredText(THEME.uiFontId, startY + LINE_SPACING * 5, ipPort, THEME.primaryTextBlack, BOLD);

  // Instructions
  renderer.drawCenteredText(THEME.smallFontId, startY + LINE_SPACING * 7,
                            "In Calibre, click:", THEME.primaryTextBlack, REGULAR);
  renderer.drawCenteredText(THEME.smallFontId, startY + LINE_SPACING * 8,
                            "Connect/Share > Start wireless", THEME.primaryTextBlack, REGULAR);
  renderer.drawCenteredText(THEME.smallFontId, startY + LINE_SPACING * 9,
                            "device connection", THEME.primaryTextBlack, REGULAR);

  // Device name from settings
  std::string deviceName = std::string("Device: ") + CALIBRE_SETTINGS.getDeviceName();
  renderer.drawCenteredText(THEME.smallFontId, startY + LINE_SPACING * 11, deviceName.c_str(),
                            THEME.primaryTextBlack, REGULAR);

  // Button hints
  const auto labels = mappedInput.mapLabels("Exit", "", "", "");
  renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4,
                           THEME.primaryTextBlack);
}

void CalibreConnectionActivity::renderConnected() const {
  constexpr int LINE_SPACING = 32;
  const int startY = 80;

  renderer.drawCenteredText(THEME.readerFontId, 40, "Calibre Wireless", THEME.primaryTextBlack, BOLD);

  renderer.drawCenteredText(THEME.uiFontId, startY + LINE_SPACING, "Connected to Calibre",
                            THEME.primaryTextBlack, BOLD);

  renderer.drawCenteredText(THEME.smallFontId, startY + LINE_SPACING * 3, currentStatus.c_str(),
                            THEME.primaryTextBlack, REGULAR);

  renderer.drawCenteredText(THEME.smallFontId, startY + LINE_SPACING * 5,
                            "You can now send books from Calibre.", THEME.primaryTextBlack, REGULAR);
  renderer.drawCenteredText(THEME.smallFontId, startY + LINE_SPACING * 6,
                            "Right-click a book > Send to device", THEME.primaryTextBlack, REGULAR);

  const auto labels = mappedInput.mapLabels("Exit", "", "", "");
  renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4,
                           THEME.primaryTextBlack);
}

void CalibreConnectionActivity::renderReceiving() const {
  constexpr int LINE_SPACING = 32;
  const int startY = 80;

  renderer.drawCenteredText(THEME.readerFontId, 40, "Calibre Wireless", THEME.primaryTextBlack, BOLD);

  renderer.drawCenteredText(THEME.uiFontId, startY + LINE_SPACING, "Receiving book...",
                            THEME.primaryTextBlack, REGULAR);

  // Book title (truncate if too long)
  std::string displayTitle = currentBookTitle;
  if (displayTitle.length() > 35) {
    displayTitle.resize(32);
    displayTitle += "...";
  }
  renderer.drawCenteredText(THEME.uiFontId, startY + LINE_SPACING * 2, displayTitle.c_str(),
                            THEME.primaryTextBlack, BOLD);

  // Progress bar
  const int pageWidth = renderer.getScreenWidth();
  const int barWidth = pageWidth - 80;
  const int barHeight = 20;
  const int barX = 40;
  const int barY = startY + LINE_SPACING * 4;

  // Draw border
  renderer.drawRect(barX, barY, barWidth, barHeight, THEME.primaryTextBlack);

  // Draw fill
  if (currentTotal > 0) {
    int fillWidth = static_cast<int>((static_cast<float>(currentProgress) / currentTotal) * (barWidth - 4));
    if (fillWidth > 0) {
      renderer.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, THEME.primaryTextBlack);
    }
  }

  // Progress text
  int percent = currentTotal > 0 ? static_cast<int>((static_cast<uint64_t>(currentProgress) * 100) / currentTotal) : 0;
  char progressText[32];
  snprintf(progressText, sizeof(progressText), "%d%%", percent);
  renderer.drawCenteredText(THEME.smallFontId, barY + barHeight + 10, progressText,
                            THEME.primaryTextBlack, REGULAR);

  // Size info
  char sizeText[64];
  if (currentTotal > 1024 * 1024) {
    snprintf(sizeText, sizeof(sizeText), "%.1f / %.1f MB",
             currentProgress / (1024.0 * 1024.0), currentTotal / (1024.0 * 1024.0));
  } else {
    snprintf(sizeText, sizeof(sizeText), "%.1f / %.1f KB",
             currentProgress / 1024.0, currentTotal / 1024.0);
  }
  renderer.drawCenteredText(THEME.smallFontId, barY + barHeight + 35, sizeText,
                            THEME.primaryTextBlack, REGULAR);
}

void CalibreConnectionActivity::renderComplete() const {
  constexpr int LINE_SPACING = 32;
  const int centerY = renderer.getScreenHeight() / 2;

  renderer.drawCenteredText(THEME.readerFontId, 40, "Calibre Wireless", THEME.primaryTextBlack, BOLD);

  renderer.drawCenteredText(THEME.uiFontId, centerY, "Book received!", THEME.primaryTextBlack, BOLD);

  std::string displayTitle = currentBookTitle;
  if (displayTitle.length() > 35) {
    displayTitle.resize(32);
    displayTitle += "...";
  }
  renderer.drawCenteredText(THEME.smallFontId, centerY + LINE_SPACING, displayTitle.c_str(),
                            THEME.primaryTextBlack, REGULAR);
}

void CalibreConnectionActivity::renderError() const {
  const int centerY = renderer.getScreenHeight() / 2;

  renderer.drawCenteredText(THEME.readerFontId, 40, "Calibre Wireless", THEME.primaryTextBlack, BOLD);

  renderer.drawCenteredText(THEME.uiFontId, centerY, "Error", THEME.primaryTextBlack, BOLD);

  renderer.drawCenteredText(THEME.smallFontId, centerY + 32, currentStatus.c_str(),
                            THEME.primaryTextBlack, REGULAR);

  const auto labels = mappedInput.mapLabels("« Back", "", "", "");
  renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4,
                           THEME.primaryTextBlack);
}
