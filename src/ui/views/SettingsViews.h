#pragma once

#include <GfxRenderer.h>
#include <Theme.h>

#include <cstdint>
#include <cstring>

#include "../Elements.h"

namespace ui {

// ============================================================================
// SettingsMenuView - Main settings category selection
// ============================================================================

struct SettingsMenuView {
  static constexpr const char* const ITEMS[] = {"Reader", "Device", "Cleanup", "System Info"};
  static constexpr int ITEM_COUNT = 4;

  ButtonBar buttons{"Back", "Open", "", ""};
  int8_t selected = 0;
  bool needsRender = true;

  void moveUp() {
    selected = (selected == 0) ? ITEM_COUNT - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % ITEM_COUNT;
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const SettingsMenuView& v);

// ============================================================================
// CleanupMenuView - Storage cleanup options
// ============================================================================

struct CleanupMenuView {
  static constexpr const char* const ITEMS[] = {"Clear Book Cache", "Clear Device Storage", "Factory Reset"};
  static constexpr int ITEM_COUNT = 3;

  ButtonBar buttons{"Back", "Run", "", ""};
  int8_t selected = 0;
  bool needsRender = true;

  void moveUp() {
    selected = (selected == 0) ? ITEM_COUNT - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % ITEM_COUNT;
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const CleanupMenuView& v);

// ============================================================================
// SystemInfoView - Device information display
// ============================================================================

struct SystemInfoView {
  static constexpr int MAX_VALUE_LEN = 32;

  struct InfoField {
    char label[24];
    char value[MAX_VALUE_LEN];
  };

  static constexpr int MAX_FIELDS = 8;
  ButtonBar buttons{"Back", "", "", ""};
  InfoField fields[MAX_FIELDS] = {};
  uint8_t fieldCount = 0;
  bool needsRender = true;

  void clear() {
    fieldCount = 0;
    needsRender = true;
  }

  void addField(const char* label, const char* value) {
    if (fieldCount < MAX_FIELDS) {
      strncpy(fields[fieldCount].label, label, sizeof(InfoField::label) - 1);
      fields[fieldCount].label[sizeof(InfoField::label) - 1] = '\0';
      strncpy(fields[fieldCount].value, value, MAX_VALUE_LEN - 1);
      fields[fieldCount].value[MAX_VALUE_LEN - 1] = '\0';
      fieldCount++;
      needsRender = true;
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, const SystemInfoView& v);

// ============================================================================
// ReaderSettingsView - Reader configuration
// ============================================================================

struct ReaderSettingsView {
  // Setting types
  enum class SettingType : uint8_t { Toggle, Enum, ThemeSelect };

  struct SettingDef {
    const char* label;
    SettingType type;
    const char* const* enumValues;
    uint8_t enumCount;
  };

  // Static setting definitions
  static constexpr const char* const FONT_SIZE_VALUES[] = {"XSmall", "Small", "Normal", "Large"};
  static constexpr const char* const TEXT_LAYOUT_VALUES[] = {"Compact", "Standard", "Large"};
  static constexpr const char* const LINE_SPACING_VALUES[] = {"Compact", "Normal", "Relaxed", "Large"};
  static constexpr const char* const ALIGNMENT_VALUES[] = {"Justified", "Left", "Center", "Right"};
  static constexpr const char* const STATUS_BAR_VALUES[] = {"None", "Show"};
  static constexpr const char* const ORIENTATION_VALUES[] = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};

  static constexpr int SETTING_COUNT = 10;
  static constexpr int MAX_THEMES = 16;
  static const SettingDef DEFS[SETTING_COUNT];

  ButtonBar buttons{"Back", "", "<", ">"};

  // Theme selection state (loaded from ThemeManager)
  char themeNames[MAX_THEMES][32] = {};
  int themeCount = 0;
  int currentThemeIndex = 0;

  // Current values (indices for enums, bool for toggles)
  uint8_t values[SETTING_COUNT] = {0};
  int8_t selected = 0;
  bool needsRender = true;

  void moveUp() {
    selected = (selected == 0) ? SETTING_COUNT - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % SETTING_COUNT;
    needsRender = true;
  }

  void cycleValue(int delta) {
    const auto& def = DEFS[selected];
    if (def.type == SettingType::Toggle) {
      values[selected] = values[selected] ? 0 : 1;
    } else if (def.type == SettingType::ThemeSelect) {
      if (themeCount > 0) {
        currentThemeIndex = (currentThemeIndex + themeCount + delta) % themeCount;
      }
    } else {
      values[selected] = static_cast<uint8_t>((values[selected] + def.enumCount + delta) % def.enumCount);
    }
    needsRender = true;
  }

  const char* getCurrentValueStr(int index) const {
    const auto& def = DEFS[index];
    if (def.type == SettingType::Toggle) {
      return values[index] ? "ON" : "OFF";
    }
    if (def.type == SettingType::ThemeSelect) {
      if (themeCount > 0 && currentThemeIndex < themeCount) {
        return themeNames[currentThemeIndex];
      }
      return "light";
    }
    // Bounds check to prevent array out-of-bounds access from corrupted settings
    if (def.enumCount == 0 || values[index] >= def.enumCount) {
      return def.enumCount > 0 ? def.enumValues[0] : "???";
    }
    return def.enumValues[values[index]];
  }

  const char* getCurrentThemeName() const {
    if (themeCount > 0 && currentThemeIndex < themeCount) {
      return themeNames[currentThemeIndex];
    }
    return "light";
  }
};

void render(const GfxRenderer& r, const Theme& t, const ReaderSettingsView& v);

// ============================================================================
// DeviceSettingsView - Device configuration
// ============================================================================

struct DeviceSettingsView {
  static constexpr const char* const SLEEP_TIMEOUT_VALUES[] = {"5 min", "10 min", "15 min", "30 min", "Never"};
  static constexpr const char* const SLEEP_SCREEN_VALUES[] = {"Dark", "Light", "Custom", "Cover"};
  static constexpr const char* const STARTUP_VALUES[] = {"Last Document", "Home"};
  static constexpr const char* const SHORT_PWR_VALUES[] = {"Ignore", "Sleep", "Page Turn"};
  static constexpr const char* const PAGES_REFRESH_VALUES[] = {"1", "5", "10", "15", "30"};
  static constexpr const char* const TOGGLE_VALUES[] = {"OFF", "ON"};
  static constexpr const char* const FRONT_BUTTON_VALUES[] = {"B/C/L/R", "L/R/B/C"};
  static constexpr const char* const SIDE_BUTTON_VALUES[] = {"Prev/Next", "Next/Prev"};

  struct SettingDef {
    const char* label;
    const char* const* values;
    uint8_t valueCount;
  };

  static constexpr int SETTING_COUNT = 8;
  static const SettingDef DEFS[SETTING_COUNT];

  ButtonBar buttons{"Back", "", "<", ">"};
  uint8_t values[SETTING_COUNT] = {0};
  int8_t selected = 0;
  bool needsRender = true;

  void moveUp() {
    selected = (selected == 0) ? SETTING_COUNT - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % SETTING_COUNT;
    needsRender = true;
  }

  void cycleValue(int delta) {
    const auto& def = DEFS[selected];
    values[selected] = static_cast<uint8_t>((values[selected] + def.valueCount + delta) % def.valueCount);
    needsRender = true;
  }

  const char* getCurrentValueStr(int index) const {
    const auto& def = DEFS[index];
    // Bounds check to prevent array out-of-bounds access from corrupted settings
    if (def.valueCount == 0 || values[index] >= def.valueCount) {
      return def.valueCount > 0 ? def.values[0] : "???";
    }
    return def.values[values[index]];
  }
};

void render(const GfxRenderer& r, const Theme& t, const DeviceSettingsView& v);

// ============================================================================
// ConfirmDialogView - Yes/No confirmation dialog (matches old ConfirmActionActivity)
// ============================================================================

struct ConfirmDialogView {
  static constexpr int MAX_TITLE_LEN = 32;
  static constexpr int MAX_LINE_LEN = 48;

  ButtonBar buttons{"Back", "Confirm", "<<", ">>"};
  char title[MAX_TITLE_LEN] = "Confirm?";
  char line1[MAX_LINE_LEN] = "";
  char line2[MAX_LINE_LEN] = "";
  int8_t selection = 1;  // 0 = Yes, 1 = No (default No for safety)
  bool needsRender = true;

  void setup(const char* t, const char* l1, const char* l2) {
    strncpy(title, t, MAX_TITLE_LEN - 1);
    title[MAX_TITLE_LEN - 1] = '\0';
    strncpy(line1, l1, MAX_LINE_LEN - 1);
    line1[MAX_LINE_LEN - 1] = '\0';
    if (l2) {
      strncpy(line2, l2, MAX_LINE_LEN - 1);
      line2[MAX_LINE_LEN - 1] = '\0';
    } else {
      line2[0] = '\0';
    }
    selection = 1;  // Default to No
    needsRender = true;
  }

  void toggleSelection() {
    selection = selection ? 0 : 1;
    needsRender = true;
  }

  bool isYesSelected() const { return selection == 0; }
};

void render(const GfxRenderer& r, const Theme& t, const ConfirmDialogView& v);

}  // namespace ui
