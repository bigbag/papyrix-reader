#include "SettingsViews.h"

#include <I18n.h>

namespace ui {

// ReaderSettingsView runtime initialization
ReaderSettingsView::SettingDef ReaderSettingsView::DEFS[SETTING_COUNT] = {};

void ReaderSettingsView::initDefs() {
  static const char* fontSizeValues[4];
  static const char* textLayoutValues[3];
  static const char* lineSpacingValues[4];
  static const char* alignmentValues[4];
  static const char* statusBarValues[3];
  static const char* orientationValues[4];

  fontSizeValues[0] = tr(XSMALL);
  fontSizeValues[1] = tr(SMALL);
  fontSizeValues[2] = tr(NORMAL);
  fontSizeValues[3] = tr(LARGE);
  textLayoutValues[0] = tr(COMPACT);
  textLayoutValues[1] = tr(STANDARD);
  textLayoutValues[2] = tr(LARGE);
  lineSpacingValues[0] = tr(COMPACT);
  lineSpacingValues[1] = tr(NORMAL);
  lineSpacingValues[2] = tr(RELAXED);
  lineSpacingValues[3] = tr(LARGE);
  alignmentValues[0] = tr(JUSTIFIED);
  alignmentValues[1] = tr(LEFT);
  alignmentValues[2] = tr(CENTER);
  alignmentValues[3] = tr(RIGHT);
  statusBarValues[0] = tr(NONE_VAL);
  statusBarValues[1] = tr(TITLE_VAL);
  statusBarValues[2] = tr(CHAPTER_VAL);
  orientationValues[0] = tr(PORTRAIT);
  orientationValues[1] = tr(LANDSCAPE_CW);
  orientationValues[2] = tr(INVERTED);
  orientationValues[3] = tr(LANDSCAPE_CCW);

  DEFS[0] = {tr(THEME), SettingType::ThemeSelect, nullptr, 0};
  DEFS[1] = {tr(FONT_SIZE), SettingType::Enum, fontSizeValues, 4};
  DEFS[2] = {tr(TEXT_LAYOUT), SettingType::Enum, textLayoutValues, 3};
  DEFS[3] = {tr(LINE_SPACING), SettingType::Enum, lineSpacingValues, 4};
  DEFS[4] = {tr(TEXT_ANTI_ALIASING), SettingType::Toggle, nullptr, 0};
  DEFS[5] = {tr(PARAGRAPH_ALIGNMENT), SettingType::Enum, alignmentValues, 4};
  DEFS[6] = {tr(HYPHENATION), SettingType::Toggle, nullptr, 0};
  DEFS[7] = {tr(SHOW_IMAGES), SettingType::Toggle, nullptr, 0};
  DEFS[8] = {tr(STATUS_BAR), SettingType::Enum, statusBarValues, 3};
  DEFS[9] = {tr(READING_ORIENTATION), SettingType::Enum, orientationValues, 4};
  DEFS[10] = {tr(FULL_BOOK_PROCESS), SettingType::Toggle, nullptr, 0};
}

// DeviceSettingsView runtime initialization
DeviceSettingsView::SettingDef DeviceSettingsView::DEFS[SETTING_COUNT] = {};

void DeviceSettingsView::initDefs() {
  static const char* sleepTimeoutValues[5];
  static const char* sleepScreenValues[4];
  static const char* startupValues[2];
  static const char* shortPwrValues[3];
  static const char* pagesRefreshValues[6];
  static const char* toggleValues[2];
  static const char* frontButtonValues[2];
  static const char* sideButtonValues[2];

  sleepTimeoutValues[0] = tr(MIN_5);
  sleepTimeoutValues[1] = tr(MIN_10);
  sleepTimeoutValues[2] = tr(MIN_15);
  sleepTimeoutValues[3] = tr(MIN_30);
  sleepTimeoutValues[4] = tr(NEVER);
  sleepScreenValues[0] = tr(DARK);
  sleepScreenValues[1] = tr(LIGHT);
  sleepScreenValues[2] = tr(CUSTOM);
  sleepScreenValues[3] = tr(COVER);
  startupValues[0] = tr(LAST_DOCUMENT);
  startupValues[1] = tr(HOME);
  shortPwrValues[0] = tr(IGNORE);
  shortPwrValues[1] = tr(SLEEP_VAL);
  shortPwrValues[2] = tr(PAGE_TURN);
  pagesRefreshValues[0] = "1";
  pagesRefreshValues[1] = "5";
  pagesRefreshValues[2] = "10";
  pagesRefreshValues[3] = "15";
  pagesRefreshValues[4] = "30";
  pagesRefreshValues[5] = tr(OFF);
  toggleValues[0] = tr(OFF);
  toggleValues[1] = tr(ON);
  frontButtonValues[0] = tr(FRONT_BCLR);
  frontButtonValues[1] = tr(FRONT_LRBC);
  sideButtonValues[0] = tr(PREV_NEXT);
  sideButtonValues[1] = tr(NEXT_PREV);

  DEFS[0] = {tr(AUTO_SLEEP_TIMEOUT), sleepTimeoutValues, 5};
  DEFS[1] = {tr(SLEEP_SCREEN), sleepScreenValues, 4};
  DEFS[2] = {tr(STARTUP_BEHAVIOR), startupValues, 2};
  DEFS[3] = {tr(SHORT_POWER_BUTTON), shortPwrValues, 3};
  DEFS[4] = {tr(PAGES_PER_REFRESH), pagesRefreshValues, 6};
  DEFS[5] = {tr(SUNLIGHT_FADING_FIX), toggleValues, 2};
  DEFS[6] = {tr(FRONT_BUTTONS), frontButtonValues, 2};
  DEFS[7] = {tr(SIDE_BUTTONS), sideButtonValues, 2};
}

// Render functions

void render(const GfxRenderer& r, const Theme& t, const SettingsMenuView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(SETTINGS));

  const char* items[] = {tr(READER), tr(DEVICE), tr(CLEANUP), tr(FIRMWARE_UPDATE), tr(SYSTEM_INFO)};
  const int startY = 60;
  for (int i = 0; i < SettingsMenuView::ITEM_COUNT; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    menuItem(r, t, y, items[i], i == v.selected);
  }

  ButtonBar btns{tr(BACK), tr(OPEN), "", ""};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const CleanupMenuView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(CLEANUP));

  const char* items[] = {tr(CLEAR_BOOK_CACHE), tr(CLEAR_DEVICE_STORAGE), tr(FACTORY_RESET)};
  const int startY = 60;
  for (int i = 0; i < CleanupMenuView::ITEM_COUNT; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    menuItem(r, t, y, items[i], i == v.selected);
  }

  ButtonBar btns{tr(BACK), tr(RUN), "", ""};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const SystemInfoView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(SYSTEM_INFO));

  const int lineHeight = r.getLineHeight(t.uiFontId) + 5;
  const int startY = 60;

  for (int i = 0; i < v.fieldCount; i++) {
    const int y = startY + i * lineHeight;
    twoColumnRow(r, t, y, v.fields[i].label, v.fields[i].value);
  }

  ButtonBar btns{tr(BACK), "", "", ""};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ReaderSettingsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(READER_SETTINGS));

  const int startY = 60;
  for (int i = 0; i < ReaderSettingsView::SETTING_COUNT; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    const auto& def = ReaderSettingsView::DEFS[i];

    enumValue(r, t, y, def.label, v.getCurrentValueStr(i), i == v.selected);
  }

  ButtonBar btns{tr(BACK), "", "<", ">"};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const DeviceSettingsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(DEVICE_SETTINGS));

  const int startY = 60;
  for (int i = 0; i < DeviceSettingsView::SETTING_COUNT; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    enumValue(r, t, y, DeviceSettingsView::DEFS[i].label, v.getCurrentValueStr(i), i == v.selected);
  }

  ButtonBar btns{tr(BACK), "", "<", ">"};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ConfirmDialogView& v) {
  const int pageWidth = r.getScreenWidth();
  const int pageHeight = r.getScreenHeight();
  const int lineHeight = r.getLineHeight(t.uiFontId);
  const int top = (pageHeight - lineHeight * 3) / 2;

  r.clearScreen(t.backgroundColor);

  r.drawCenteredText(t.readerFontId, top - 40, v.title, t.primaryTextBlack, EpdFontFamily::BOLD);

  r.drawCenteredText(t.uiFontId, top, v.line1, t.primaryTextBlack);
  if (v.line2[0] != '\0') {
    r.drawCenteredText(t.uiFontId, top + lineHeight, v.line2, t.primaryTextBlack);
  }

  const int buttonY = top + lineHeight * 3;
  constexpr int buttonWidth = 80;
  constexpr int buttonHeight = 36;
  constexpr int buttonSpacing = 20;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (pageWidth - totalWidth) / 2;

  const char* buttonLabels[] = {tr(YES), tr(NO)};
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

  ButtonBar btns{tr(BACK), tr(CONFIRM), "<<", ">>"};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const FirmwareUpdateView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(FIRMWARE_UPDATE));

  const int lineHeight = r.getLineHeight(t.uiFontId) + 8;
  const int startY = 60;

  const int marginX = t.screenMarginSide + t.itemPaddingX;
  const int maxTextWidth = r.getScreenWidth() - marginX * 2;
  auto warningLines = r.wrapTextWithHyphenation(t.uiFontId, tr(FIRMWARE_WARNING), maxTextWidth, 2);
  for (size_t i = 0; i < warningLines.size(); i++) {
    r.drawText(t.uiFontId, marginX, startY + lineHeight * (1 + static_cast<int>(i)), warningLines[i].c_str(),
               t.primaryTextBlack);
  }

  const int statusY = startY + lineHeight * 3;
  r.drawCenteredText(t.uiFontId, statusY, v.statusLine, t.primaryTextBlack);

  if (v.state == FirmwareUpdateView::State::Flashing) {
    progress(r, t, statusY + lineHeight, v.progressPercent, 100);
  }

  if (v.state == FirmwareUpdateView::State::Error) {
    r.drawCenteredText(t.uiFontId, statusY + lineHeight * 2, tr(PRESS_ANY_BUTTON), t.primaryTextBlack);
  }

  bool interactive = v.state == FirmwareUpdateView::State::Idle || v.state == FirmwareUpdateView::State::Error;
  ButtonBar btns{interactive ? tr(BACK) : "", interactive ? tr(RUN) : "", "", ""};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

}  // namespace ui
