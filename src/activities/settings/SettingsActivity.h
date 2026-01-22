#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "activities/ActivityWithSubactivity.h"

class SettingsActivity final : public ActivityWithSubactivity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  int selectedIndex = 0;
  const std::function<void()> onGoHome;
  const std::function<void()> onFileTransferOpen;
  const std::function<void()> onOpdsLibraryOpen;
  const std::function<void()> onCalibreWirelessOpen;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void openSelectedCategory();

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const std::function<void()>& onGoHome, const std::function<void()>& onFileTransferOpen,
                            const std::function<void()>& onOpdsLibraryOpen,
                            const std::function<void()>& onCalibreWirelessOpen)
      : ActivityWithSubactivity("Settings", renderer, mappedInput),
        onGoHome(onGoHome),
        onFileTransferOpen(onFileTransferOpen),
        onOpdsLibraryOpen(onOpdsLibraryOpen),
        onCalibreWirelessOpen(onCalibreWirelessOpen) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
