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

struct SettingInfo {
  const char* name;
  SettingType type;
  uint8_t CrossPointSettings::* valuePtr;
  const char* const* enumValues;
  uint8_t enumCount;
};

class SettingsListActivity : public ActivityWithSubactivity {
 protected:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  int selectedIndex = 0;
  const std::function<void()> onComplete;
  const char* title;

  // Theme selection state (used by ReaderSettingsActivity)
  std::vector<std::string> availableThemes;
  int currentThemeIndex = 0;
  bool themeWasChanged = false;

  // Subclasses must provide settings
  virtual const SettingInfo* getSettings() const = 0;
  virtual int getSettingsCount() const = 0;

  // Optional: handle actions (override in ToolsSettingsActivity)
  virtual void handleAction(const char* actionName);

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
  void toggleCurrentSetting();
  void loadAvailableThemes();

 public:
  explicit SettingsListActivity(const char* name, const char* title, GfxRenderer& renderer,
                                MappedInputManager& mappedInput, std::function<void()> onComplete)
      : ActivityWithSubactivity(name, renderer, mappedInput), onComplete(std::move(onComplete)), title(title) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
