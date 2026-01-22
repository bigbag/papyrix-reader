#pragma once
#include "CrossPointSettings.h"
#include "SettingsListActivity.h"

class DeviceSettingsActivity final : public SettingsListActivity {
  static constexpr const char* pagesPerRefreshValues[] = {"1", "5", "10", "15", "30"};
  static constexpr const char* autoSleepValues[] = {"5 min", "10 min", "15 min", "30 min", "Never"};
  static constexpr const char* startupBehaviorValues[] = {"Last Document", "Home"};
  static constexpr const char* sleepScreenValues[] = {"Dark", "Light", "Custom", "Cover"};
  static constexpr const char* shortPwrBtnValues[] = {"Ignore", "Sleep", "Page Turn"};

  static constexpr SettingInfo settings[] = {
      {"Auto Sleep Timeout", SettingType::ENUM, &CrossPointSettings::autoSleepMinutes, autoSleepValues, 5},
      {"Sleep Screen", SettingType::ENUM, &CrossPointSettings::sleepScreen, sleepScreenValues, 4},
      {"Startup Behavior", SettingType::ENUM, &CrossPointSettings::startupBehavior, startupBehaviorValues, 2},
      {"Short Power Button", SettingType::ENUM, &CrossPointSettings::shortPwrBtn, shortPwrBtnValues, 3},
      {"Pages Per Refresh", SettingType::ENUM, &CrossPointSettings::pagesPerRefresh, pagesPerRefreshValues, 5},
  };
  static constexpr int settingsCount = sizeof(settings) / sizeof(settings[0]);

 protected:
  const SettingInfo* getSettings() const override { return settings; }
  int getSettingsCount() const override { return settingsCount; }

 public:
  explicit DeviceSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  std::function<void()> onComplete)
      : SettingsListActivity("DeviceSettings", "Device", renderer, mappedInput, std::move(onComplete)) {}
};
