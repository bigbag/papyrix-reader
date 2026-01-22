#pragma once
#include "SettingsListActivity.h"

class ToolsSettingsActivity final : public SettingsListActivity {
  static constexpr SettingInfo settings[] = {
      {"File Transfer", SettingType::ACTION, nullptr, nullptr, 0},
      {"Net Library", SettingType::ACTION, nullptr, nullptr, 0},
      {"Calibre Wireless", SettingType::ACTION, nullptr, nullptr, 0},
      {"Cleanup", SettingType::ACTION, nullptr, nullptr, 0},
  };
  static constexpr int settingsCount = sizeof(settings) / sizeof(settings[0]);

  const std::function<void()> onOpdsLibraryOpen;
  const std::function<void()> onCalibreWirelessOpen;
  const std::function<void()> onFileTransferOpen;

 protected:
  const SettingInfo* getSettings() const override { return settings; }
  int getSettingsCount() const override { return settingsCount; }
  void handleAction(const char* actionName) override;

 public:
  explicit ToolsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::function<void()> onComplete, std::function<void()> onOpdsLibraryOpen,
                                 std::function<void()> onCalibreWirelessOpen, std::function<void()> onFileTransferOpen)
      : SettingsListActivity("ToolsSettings", "Tools", renderer, mappedInput, std::move(onComplete)),
        onOpdsLibraryOpen(std::move(onOpdsLibraryOpen)),
        onCalibreWirelessOpen(std::move(onCalibreWirelessOpen)),
        onFileTransferOpen(std::move(onFileTransferOpen)) {}
};
