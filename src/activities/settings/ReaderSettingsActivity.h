#pragma once
#include "CrossPointSettings.h"
#include "SettingsListActivity.h"

class ReaderSettingsActivity final : public SettingsListActivity {
  static constexpr const char* fontSizeValues[] = {"Small", "Normal", "Large"};
  static constexpr const char* textLayoutValues[] = {"Compact", "Standard", "Large"};
  static constexpr const char* paragraphAlignmentValues[] = {"Justified", "Left", "Center", "Right"};
  static constexpr const char* statusBarValues[] = {"None", "No Progress", "Full"};
  static constexpr const char* orientationValues[] = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};

  static constexpr SettingInfo settings[] = {
      {"Theme", SettingType::THEME_SELECT, nullptr, nullptr, 0},
      {"Font Size", SettingType::ENUM, &CrossPointSettings::fontSize, fontSizeValues, 3},
      {"Text Layout", SettingType::ENUM, &CrossPointSettings::textLayout, textLayoutValues, 3},
      {"Text Anti-Aliasing", SettingType::TOGGLE, &CrossPointSettings::textAntiAliasing, nullptr, 0},
      {"Paragraph Alignment", SettingType::ENUM, &CrossPointSettings::paragraphAlignment, paragraphAlignmentValues, 4},
      {"Hyphenation", SettingType::TOGGLE, &CrossPointSettings::hyphenation, nullptr, 0},
      {"Show Images", SettingType::TOGGLE, &CrossPointSettings::showImages, nullptr, 0},
      {"Cover Dithering", SettingType::TOGGLE, &CrossPointSettings::coverDithering, nullptr, 0},
      {"Status Bar", SettingType::ENUM, &CrossPointSettings::statusBar, statusBarValues, 3},
      {"Reading Orientation", SettingType::ENUM, &CrossPointSettings::orientation, orientationValues, 4},
  };
  static constexpr int settingsCount = sizeof(settings) / sizeof(settings[0]);

 protected:
  const SettingInfo* getSettings() const override { return settings; }
  int getSettingsCount() const override { return settingsCount; }

 public:
  explicit ReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  std::function<void()> onComplete)
      : SettingsListActivity("ReaderSettings", "Reader", renderer, mappedInput, std::move(onComplete)) {}
};
