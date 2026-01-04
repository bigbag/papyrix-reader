#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <memory>
#include <string>

#include "activities/ActivityWithSubactivity.h"
#include "network/CalibreDeviceServer.h"

// Calibre connection activity states
enum class CalibreActivityState {
  WIFI_SELECTION,     // WiFi selection subactivity is active
  STARTING_SERVER,    // Initializing Calibre server
  WAITING,            // Server running, waiting for Calibre to connect
  CONNECTED,          // Calibre is connected
  RECEIVING_BOOK,     // Actively receiving a book
  TRANSFER_COMPLETE,  // Book transfer completed successfully
  ERROR,              // Error state
  SHUTTING_DOWN       // Shutting down server and WiFi
};

/**
 * CalibreConnectionActivity provides wireless device connection to Calibre.
 *
 * Features:
 * - Connects to WiFi using WifiSelectionActivity
 * - Runs Calibre Device Server for UDP discovery and TCP protocol
 * - Displays connection status and transfer progress
 * - Supports bidirectional sync: receive books, send book list, delete books
 */
class CalibreConnectionActivity final : public ActivityWithSubactivity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  CalibreActivityState state = CalibreActivityState::WIFI_SELECTION;
  const std::function<void()> onGoBack;

  // Calibre server
  std::unique_ptr<CalibreDeviceServer> calibreServer;

  // Connection info
  std::string connectedIP;
  std::string connectedSSID;

  // Transfer state for display
  std::string currentStatus;
  std::string currentBookTitle;
  size_t currentProgress = 0;
  size_t currentTotal = 0;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void renderWaiting() const;
  void renderConnected() const;
  void renderReceiving() const;
  void renderComplete() const;
  void renderError() const;

  void onWifiSelectionComplete(bool connected);
  void startCalibreServer();
  void stopCalibreServer();

  // Server callbacks
  void onServerStatus(const char* status);
  void onServerProgress(const char* title, size_t received, size_t total);
  void onBookReceived(const char* path);
  void onBookDeleted(const char* path);

 public:
  explicit CalibreConnectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const std::function<void()>& onGoBack)
      : ActivityWithSubactivity("CalibreConnection", renderer, mappedInput), onGoBack(onGoBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return calibreServer && calibreServer->isRunning(); }
  bool preventAutoSleep() override { return calibreServer && calibreServer->isRunning(); }
};
