#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "activities/ActivityWithSubactivity.h"

class CrossPointSettings;

enum class SettingType { TOGGLE, ENUM, ACTION, THEME_SELECT };

// Structure to hold setting information
struct SettingInfo {
  const char* name;                        // Display name of the setting
  SettingType type;                        // Type of setting
  uint8_t CrossPointSettings::* valuePtr;  // Pointer to member in CrossPointSettings (for TOGGLE/ENUM)
  const char* const* enumValues;           // Pointer to const char* array (for ENUM)
  uint8_t enumCount;                       // Number of enum values
};

class SettingsActivity final : public ActivityWithSubactivity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  int selectedSettingIndex = 0;  // Currently selected setting
  const std::function<void()> onGoHome;
  const std::function<void()> onFileTransferOpen;
  const std::function<void()> onOpdsLibraryOpen;
  const std::function<void()> onCalibreWirelessOpen;
  const int initialSelectedIndex;

  // Theme selection state
  std::vector<std::string> availableThemes;
  int currentThemeIndex = 0;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void toggleCurrentSetting();
  void loadAvailableThemes();

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const std::function<void()>& onGoHome,
                            const std::function<void()>& onFileTransferOpen,
                            const std::function<void()>& onOpdsLibraryOpen,
                            const std::function<void()>& onCalibreWirelessOpen,
                            int initialSelectedIndex = 0)
      : ActivityWithSubactivity("Settings", renderer, mappedInput),
        onGoHome(onGoHome),
        onFileTransferOpen(onFileTransferOpen),
        onOpdsLibraryOpen(onOpdsLibraryOpen),
        onCalibreWirelessOpen(onCalibreWirelessOpen),
        initialSelectedIndex(initialSelectedIndex) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
