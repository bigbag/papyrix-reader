#pragma once

#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../Elements.h"

namespace ui {
struct SettingsListHit {
  static constexpr int LIST_START_Y = 60;
  static constexpr int LIST_BOTTOM_MARGIN = 70;
  enum class Type : uint8_t { None, Row, Back, Open, Previous, Next };
  Type type = Type::None;
  int index = -1;
};

inline SettingsListHit settingsListHitTest(touch::Point point, int16_t screenWidth, int16_t screenHeight,
                                           int16_t rowHeight, int rowCount, bool frontLrbc = false) {
  const int action = touch::semanticButtonBarIndex(point, screenWidth, screenHeight, frontLrbc);
  if (action >= 0) {
    constexpr SettingsListHit::Type actions[] = {SettingsListHit::Type::Back, SettingsListHit::Type::Open,
                                                 SettingsListHit::Type::Previous, SettingsListHit::Type::Next};
    return {actions[action], -1};
  }
  const int row = touch::rowAt(
      point,
      {0, SettingsListHit::LIST_START_Y, screenWidth,
       static_cast<int16_t>(screenHeight - SettingsListHit::LIST_START_Y - SettingsListHit::LIST_BOTTOM_MARGIN)},
      rowHeight, rowCount);
  return row < 0 ? SettingsListHit{} : SettingsListHit{SettingsListHit::Type::Row, row};
}

// ============================================================================
// SettingsMenuView - Main settings category selection
// ============================================================================

struct SettingsMenuView {
  enum class Item : int8_t { Reader, Screen, Device, Cleanup, FirmwareUpdate, SystemInfo, Count };
  static constexpr int ITEM_COUNT = static_cast<int>(Item::Count);

  ButtonBar buttons;
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

  Item selectedItem() const { return static_cast<Item>(selected); }
};

void render(const GfxRenderer& r, const Theme& t, const SettingsMenuView& v);

// ============================================================================
// CleanupMenuView - Storage cleanup options
// ============================================================================

struct CleanupMenuView {
  enum class Item : int8_t { ClearBookCache, ClearRecent, EmptyTrash, ClearDeviceStorage, FactoryReset, Count };
  static constexpr int ITEM_COUNT = static_cast<int>(Item::Count);

  ButtonBar buttons;
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

  Item selectedItem() const { return static_cast<Item>(selected); }
};

void render(const GfxRenderer& r, const Theme& t, const CleanupMenuView& v);

// ============================================================================
// SystemInfoView - Device information display
// ============================================================================

struct SystemInfoView {
  static constexpr int MAX_VALUE_LEN = 32;

  struct InfoField {
    char label[40];
    char value[MAX_VALUE_LEN];
  };

  enum class Field : uint8_t {
    Version,
    Uptime,
    Battery,
    Chip,
    Cpu,
    FreeMemory,
    InternalDisk,
    SdCard,
    Count,
  };

  static constexpr size_t FIELD_COUNT = static_cast<size_t>(Field::Count);
  ButtonBar buttons;
  InfoField fields[FIELD_COUNT] = {};
  bool needsRender = true;

  void clear() {
    memset(fields, 0, sizeof(fields));
    needsRender = true;
  }

  bool setField(Field field, const char* label, const char* value) {
    const size_t index = static_cast<size_t>(field);
    if (index >= FIELD_COUNT) return false;
    strncpy(fields[index].label, label, sizeof(InfoField::label) - 1);
    fields[index].label[sizeof(InfoField::label) - 1] = '\0';
    strncpy(fields[index].value, value, MAX_VALUE_LEN - 1);
    fields[index].value[MAX_VALUE_LEN - 1] = '\0';
    needsRender = true;
    return true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const SystemInfoView& v);

// ============================================================================
// ReaderSettingsView - Reader configuration
// ============================================================================

struct ReaderSettingsView {
  enum class SettingType : uint8_t { Toggle, Enum };

  struct SettingDef {
    const char* label;
    SettingType type;
    const char* const* enumValues;
    uint8_t enumCount;
  };

  static constexpr int SETTING_COUNT = 9;
  static SettingDef DEFS[SETTING_COUNT];
  static void initDefs();

  ButtonBar buttons{"", "", "<", ">"};

  uint8_t values[SETTING_COUNT] = {0};
  int8_t selected = 0;
  int8_t visibleCount = SETTING_COUNT - 1;
  bool needsRender = true;

  int settingIndex(int row) const { return row + (visibleCount < SETTING_COUNT && row >= 7 ? 1 : 0); }

  void moveUp() {
    selected = (selected == 0) ? visibleCount - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % visibleCount;
    needsRender = true;
  }

  void cycleValue(int delta) {
    const int index = settingIndex(selected);
    const auto& def = DEFS[index];
    if (def.type == SettingType::Toggle) {
      values[index] = values[index] ? 0 : 1;
    } else {
      values[index] = static_cast<uint8_t>((values[index] + def.enumCount + delta) % def.enumCount);
    }
    needsRender = true;
  }

  const char* getCurrentValueStr(int index) const {
    const auto& def = DEFS[index];
    if (def.type == SettingType::Toggle) {
      return values[index] ? tr(ON) : tr(OFF);
    }
    if (def.enumCount == 0 || values[index] >= def.enumCount) {
      return def.enumCount > 0 ? def.enumValues[0] : "???";
    }
    return def.enumValues[values[index]];
  }
};

void render(const GfxRenderer& r, const Theme& t, const ReaderSettingsView& v);

struct ScreenSettingsView {
  using SettingDef = ReaderSettingsView::SettingDef;
  using SettingType = ReaderSettingsView::SettingType;
  static constexpr int SETTING_COUNT = 10;
  static constexpr int MAX_THEMES = 16;
  static SettingDef DEFS[SETTING_COUNT];
  static void initDefs();

  ButtonBar buttons{"", "", "<", ">"};
  char themeNames[MAX_THEMES][32] = {};
  int themeCount = 0;
  int currentThemeIndex = 0;
  uint8_t values[SETTING_COUNT] = {0};
  int8_t selected = 0;
  int8_t visibleCount = SETTING_COUNT - 4;
  bool needsRender = true;

  int settingIndex(int row) const { return row + (visibleCount < SETTING_COUNT && row >= 1 ? 4 : 0); }
  int rowHeight(const Theme& theme, int screenHeight) const {
    const int available = screenHeight - SettingsListHit::LIST_START_Y - SettingsListHit::LIST_BOTTOM_MARGIN;
    return std::max(1, std::min(theme.itemHeight + theme.itemSpacing, available / visibleCount));
  }
  bool lightSelected() const {
    const int index = settingIndex(selected);
    return index == 1 || index == 2;
  }
  uint8_t adjustedLightValue(int delta) const {
    return static_cast<uint8_t>(std::clamp(values[settingIndex(selected)] + delta * 5, 0, 100));
  }
  void moveUp() {
    selected = (selected == 0) ? visibleCount - 1 : selected - 1;
    needsRender = true;
  }
  void moveDown() {
    selected = (selected + 1) % visibleCount;
    needsRender = true;
  }
  void cycleValue(int delta) {
    const int index = settingIndex(selected);
    if (index == 0) {
      if (themeCount > 0) currentThemeIndex = (currentThemeIndex + themeCount + delta) % themeCount;
    } else if (!lightSelected()) {
      const auto& def = DEFS[index];
      if (def.type == SettingType::Toggle) {
        values[index] = values[index] ? 0 : 1;
      } else {
        values[index] = static_cast<uint8_t>((values[index] + def.enumCount + delta) % def.enumCount);
      }
    }
    needsRender = true;
  }
  const char* getCurrentThemeName() const {
    return themeCount > 0 && currentThemeIndex < themeCount ? themeNames[currentThemeIndex] : "light";
  }
  const char* getCurrentValueStr(int index) const {
    if (index == 0) return getCurrentThemeName();
    const auto& def = DEFS[index];
    if (def.type == SettingType::Toggle) return values[index] ? tr(ON) : tr(OFF);
    if (def.enumCount == 0 || values[index] >= def.enumCount) {
      return def.enumCount > 0 ? def.enumValues[0] : "???";
    }
    return def.enumValues[values[index]];
  }
};

void render(const GfxRenderer& r, const Theme& t, const ScreenSettingsView& v);

// ============================================================================
// DeviceSettingsView - Device configuration
// ============================================================================

struct DeviceSettingsView {
  struct SettingDef {
    const char* label;
    const char* const* values;
    uint8_t valueCount;
  };

  static constexpr int BATCH_SETTING = 7;
  static constexpr int SETTING_COUNT = 8;
  static SettingDef DEFS[SETTING_COUNT];
  static void initDefs();

  ButtonBar buttons{"", "", "<", ">"};
  uint8_t values[SETTING_COUNT] = {0};
  int8_t selected = 0;
  int8_t visibleCount = SETTING_COUNT;
  bool needsRender = true;
  int settingIndex(int row) const { return row - 1; }

  void moveUp() {
    selected = (selected == 0) ? visibleCount - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % visibleCount;
    needsRender = true;
  }

  void cycleValue(int delta) {
    const int index = settingIndex(selected);
    if (index < 0 || index >= SETTING_COUNT) return;
    const auto& def = DEFS[index];
    values[index] = static_cast<uint8_t>((values[index] + def.valueCount + delta) % def.valueCount);
    needsRender = true;
  }

  const char* getCurrentValueStr(int index) const {
    const auto& def = DEFS[index];
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

inline touch::DialogLayout confirmDialogLayout(int16_t pageWidth, int16_t pageHeight, int16_t lineHeight,
                                               int messageLines) {
  const int16_t top = (pageHeight - lineHeight * 3) / 2;
  const int16_t buttonY = top + (messageLines + 1 < 3 ? 3 : messageLines + 1) * lineHeight;
  constexpr int16_t buttonWidth = 80;
  constexpr int16_t buttonHeight = 36;
  constexpr int16_t buttonSpacing = 20;
  const int16_t startX = (pageWidth - (buttonWidth * 2 + buttonSpacing)) / 2;
  return {{0, 0, pageWidth, pageHeight},
          {{startX, buttonY, buttonWidth, buttonHeight},
           {static_cast<int16_t>(startX + buttonWidth + buttonSpacing), buttonY, buttonWidth, buttonHeight}}};
}

struct ConfirmDialogView {
  enum class Hit : uint8_t { None, Yes, No, Back, Select };
  static constexpr int MAX_TITLE_LEN = 48;
  static constexpr int MAX_LINE_LEN = 80;
  static constexpr int MAX_TITLE_LINES = 2;
  static constexpr int MAX_MESSAGE_LINES = 2;

  ButtonBar buttons;
  char title[MAX_TITLE_LEN] = "";
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

  Hit hitTest(touch::Point point, const touch::DialogLayout& layout, bool frontLrbc = false) const {
    const int choice = touch::dialogChoiceAt(point, layout);
    if (choice == 0) return Hit::Yes;
    if (choice == 1) return Hit::No;
    const int action = touch::semanticButtonBarIndex(point, layout.bounds.width, layout.bounds.height, frontLrbc);
    if (action == 0) return Hit::Back;
    if (action == 1) return Hit::Select;
    return Hit::None;
  }
};

void render(const GfxRenderer& r, const Theme& t, const ConfirmDialogView& v);
touch::DialogLayout confirmDialogBounds(const GfxRenderer& r, const Theme& t, const ConfirmDialogView& v);

// ============================================================================
// FirmwareUpdateView - Firmware update from SD card
// ============================================================================

struct FirmwareUpdateView {
  static constexpr int MAX_STATUS_LINES = 2;

  enum class State : uint8_t { Idle, Validating, Flashing, Complete, Error };

  ButtonBar buttons;
  State state = State::Idle;
  char statusLine[128] = "";
  int progressPercent = 0;
  bool needsRender = true;
};

void render(const GfxRenderer& r, const Theme& t, const FirmwareUpdateView& v);

}  // namespace ui
