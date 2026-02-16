#include "SettingsViews.h"

namespace ui {

// Static definitions for constexpr arrays
constexpr const char* const SettingsMenuView::ITEMS[];
constexpr const char* const CleanupMenuView::ITEMS[];

// ReaderSettingsView static definitions
constexpr const char* const ReaderSettingsView::FONT_SIZE_VALUES[];
constexpr const char* const ReaderSettingsView::TEXT_LAYOUT_VALUES[];
constexpr const char* const ReaderSettingsView::LINE_SPACING_VALUES[];
constexpr const char* const ReaderSettingsView::ALIGNMENT_VALUES[];
constexpr const char* const ReaderSettingsView::STATUS_BAR_VALUES[];
constexpr const char* const ReaderSettingsView::ORIENTATION_VALUES[];

const ReaderSettingsView::SettingDef ReaderSettingsView::DEFS[SETTING_COUNT] = {
    {"Theme", SettingType::ThemeSelect, nullptr, 0},
    {"Font Size", SettingType::Enum, FONT_SIZE_VALUES, 4},
    {"Text Layout", SettingType::Enum, TEXT_LAYOUT_VALUES, 3},
    {"Line Spacing", SettingType::Enum, LINE_SPACING_VALUES, 4},
    {"Text Anti-Aliasing", SettingType::Toggle, nullptr, 0},
    {"Paragraph Alignment", SettingType::Enum, ALIGNMENT_VALUES, 4},
    {"Hyphenation", SettingType::Toggle, nullptr, 0},
    {"Show Images", SettingType::Toggle, nullptr, 0},
    {"Status Bar", SettingType::Enum, STATUS_BAR_VALUES, 2},
    {"Reading Orientation", SettingType::Enum, ORIENTATION_VALUES, 4},
};

// DeviceSettingsView static definitions
constexpr const char* const DeviceSettingsView::SLEEP_TIMEOUT_VALUES[];
constexpr const char* const DeviceSettingsView::SLEEP_SCREEN_VALUES[];
constexpr const char* const DeviceSettingsView::STARTUP_VALUES[];
constexpr const char* const DeviceSettingsView::SHORT_PWR_VALUES[];
constexpr const char* const DeviceSettingsView::PAGES_REFRESH_VALUES[];
constexpr const char* const DeviceSettingsView::TOGGLE_VALUES[];
constexpr const char* const DeviceSettingsView::FRONT_BUTTON_VALUES[];
constexpr const char* const DeviceSettingsView::SIDE_BUTTON_VALUES[];

const DeviceSettingsView::SettingDef DeviceSettingsView::DEFS[SETTING_COUNT] = {
    {"Auto Sleep Timeout", SLEEP_TIMEOUT_VALUES, 5}, {"Sleep Screen", SLEEP_SCREEN_VALUES, 4},
    {"Startup Behavior", STARTUP_VALUES, 2},         {"Short Power Button", SHORT_PWR_VALUES, 3},
    {"Pages Per Refresh", PAGES_REFRESH_VALUES, 5},  {"Sunlight Fading Fix", TOGGLE_VALUES, 2},
    {"Front Buttons", FRONT_BUTTON_VALUES, 2},       {"Side Buttons", SIDE_BUTTON_VALUES, 2},
};

// Render functions

void render(const GfxRenderer& r, const Theme& t, const SettingsMenuView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Settings");

  const int startY = 60;
  for (int i = 0; i < SettingsMenuView::ITEM_COUNT; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    menuItem(r, t, y, SettingsMenuView::ITEMS[i], i == v.selected);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const CleanupMenuView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Cleanup");

  const int startY = 60;
  for (int i = 0; i < CleanupMenuView::ITEM_COUNT; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    menuItem(r, t, y, CleanupMenuView::ITEMS[i], i == v.selected);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const SystemInfoView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "System Info");

  const int lineHeight = r.getLineHeight(t.uiFontId) + 5;
  const int startY = 60;

  for (int i = 0; i < v.fieldCount; i++) {
    const int y = startY + i * lineHeight;
    twoColumnRow(r, t, y, v.fields[i].label, v.fields[i].value);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ReaderSettingsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Reader Settings");

  const int startY = 60;
  for (int i = 0; i < ReaderSettingsView::SETTING_COUNT; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    const auto& def = ReaderSettingsView::DEFS[i];

    enumValue(r, t, y, def.label, v.getCurrentValueStr(i), i == v.selected);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const DeviceSettingsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Device Settings");

  const int startY = 60;
  for (int i = 0; i < DeviceSettingsView::SETTING_COUNT; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    enumValue(r, t, y, DeviceSettingsView::DEFS[i].label, v.getCurrentValueStr(i), i == v.selected);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ConfirmDialogView& v) {
  const int pageWidth = r.getScreenWidth();
  const int pageHeight = r.getScreenHeight();
  const int lineHeight = r.getLineHeight(t.uiFontId);
  const int top = (pageHeight - lineHeight * 3) / 2;

  r.clearScreen(t.backgroundColor);

  // Title (bold, centered)
  r.drawCenteredText(t.readerFontId, top - 40, v.title, t.primaryTextBlack, EpdFontFamily::BOLD);

  // Description lines
  r.drawCenteredText(t.uiFontId, top, v.line1, t.primaryTextBlack);
  if (v.line2[0] != '\0') {
    r.drawCenteredText(t.uiFontId, top + lineHeight, v.line2, t.primaryTextBlack);
  }

  // Yes/No buttons
  const int buttonY = top + lineHeight * 3;
  constexpr int buttonWidth = 80;
  constexpr int buttonHeight = 36;
  constexpr int buttonSpacing = 20;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (pageWidth - totalWidth) / 2;

  const char* buttonLabels[] = {"Yes", "No"};
  const int buttonPositions[] = {startX, startX + buttonWidth + buttonSpacing};

  for (int i = 0; i < 2; i++) {
    const bool isSelected = (v.selection == i);
    const int btnX = buttonPositions[i];

    if (isSelected) {
      r.fillRect(btnX, buttonY, buttonWidth, buttonHeight, t.selectionFillBlack);
    } else {
      r.drawRect(btnX, buttonY, buttonWidth, buttonHeight, t.primaryTextBlack);
    }

    const bool textColor = isSelected ? t.selectionTextBlack : t.primaryTextBlack;
    const int textWidth = r.getTextWidth(t.uiFontId, buttonLabels[i]);
    const int textX = btnX + (buttonWidth - textWidth) / 2;
    const int textY = buttonY + (buttonHeight - r.getFontAscenderSize(t.uiFontId)) / 2;
    r.drawText(t.uiFontId, textX, textY, buttonLabels[i], textColor);
  }

  // Button hints
  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

}  // namespace ui
