#pragma once
#include <OpdsParser.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../ActivityWithSubactivity.h"
#include "opds/OpdsServerStore.h"

class OpdsBookBrowserActivity final : public ActivityWithSubactivity {
 public:
  enum class BrowserState { WIFI_CHECK, LOADING, BROWSING, DOWNLOADING, ERROR };

  explicit OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const OpdsServerConfig& serverConfig, const std::function<void()>& onGoBack)
      : ActivityWithSubactivity("OpdsBookBrowser", renderer, mappedInput),
        serverConfig(serverConfig),
        onGoBack(onGoBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return true; }

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  std::atomic<BrowserState> state{BrowserState::WIFI_CHECK};
  std::vector<OpdsEntry> entries;
  std::vector<std::string> navigationHistory;
  std::string currentPath;
  int selectorIndex = 0;
  std::string errorMessage;
  std::string statusMessage;
  std::string currentSearchTemplate;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;

  const OpdsServerConfig serverConfig;
  const std::function<void()> onGoBack;

  void checkWifiConnection();
  void onWifiSelectionComplete(bool success);

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

  void fetchFeed(const std::string& path);
  void navigateToEntry(const OpdsEntry& entry);
  void navigateBack();
  void downloadBook(const OpdsEntry& book);
  void handleSearchEntry();
  std::string fetchOpenSearchTemplate(const std::string& url);
  std::string sanitizeFilename(const std::string& title) const;
};
