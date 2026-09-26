#include "SettingsViews.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace ui {

// ReaderSettingsView runtime initialization
ReaderSettingsView::SettingDef ReaderSettingsView::DEFS[SETTING_COUNT] = {};

void ReaderSettingsView::initDefs() {
  static const char* fontSizeValues[4];
  static const char* textLayoutValues[3];
  static const char* lineSpacingValues[4];
  static const char* alignmentValues[4];
  static const char* statusBarValues[4];

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
  statusBarValues[3] = tr(FILENAME_VAL);

  DEFS[0] = {tr(FONT_SIZE), SettingType::Enum, fontSizeValues, 4};
  DEFS[1] = {tr(TEXT_LAYOUT), SettingType::Enum, textLayoutValues, 3};
  DEFS[2] = {tr(LINE_SPACING), SettingType::Enum, lineSpacingValues, 4};
  DEFS[3] = {tr(PARAGRAPH_ALIGNMENT), SettingType::Enum, alignmentValues, 4};
  DEFS[4] = {tr(HYPHENATION), SettingType::Toggle, nullptr, 0};
  DEFS[5] = {tr(SHOW_IMAGES), SettingType::Toggle, nullptr, 0};
  DEFS[6] = {tr(STATUS_BAR), SettingType::Enum, statusBarValues, 4};
  DEFS[7] = {tr(TOUCH_PAGE_TURNS), SettingType::Toggle, nullptr, 0};
  DEFS[8] = {tr(FULL_BOOK_PROCESS), SettingType::Toggle, nullptr, 0};
}

ScreenSettingsView::SettingDef ScreenSettingsView::DEFS[SETTING_COUNT] = {};

void ScreenSettingsView::initDefs() {
  static const char* orientationValues[4];
  static const char* pagesRefreshValues[6];
  static const char* sleepScreenValues[5];
  orientationValues[0] = tr(PORTRAIT);
  orientationValues[1] = tr(LANDSCAPE_CW);
  orientationValues[2] = tr(INVERTED);
  orientationValues[3] = tr(LANDSCAPE_CCW);
  pagesRefreshValues[0] = "1";
  pagesRefreshValues[1] = "5";
  pagesRefreshValues[2] = "10";
  pagesRefreshValues[3] = "15";
  pagesRefreshValues[4] = "30";
  pagesRefreshValues[5] = tr(OFF);
  sleepScreenValues[0] = tr(DARK);
  sleepScreenValues[1] = tr(LIGHT);
  sleepScreenValues[2] = tr(CUSTOM);
  sleepScreenValues[3] = tr(COVER);
  sleepScreenValues[4] = tr(KEEP_PAGE);
  DEFS[0] = {tr(THEME), SettingType::Enum, nullptr, 0};
  DEFS[1] = {tr(BRIGHTNESS), SettingType::Enum, nullptr, 0};
  DEFS[2] = {tr(WARMTH), SettingType::Enum, nullptr, 0};
  DEFS[3] = {tr(READING_ORIENTATION), SettingType::Enum, orientationValues, 4};
  DEFS[4] = {tr(TEXT_ANTI_ALIASING), SettingType::Toggle, nullptr, 0};
  DEFS[5] = {tr(PAGES_PER_REFRESH), SettingType::Enum, pagesRefreshValues, 6};
  DEFS[6] = {tr(SUNLIGHT_FADING_FIX), SettingType::Toggle, nullptr, 0};
  DEFS[7] = {tr(SLEEP_SCREEN), SettingType::Enum, sleepScreenValues, 5};
}

// DeviceSettingsView runtime initialization
DeviceSettingsView::SettingDef DeviceSettingsView::DEFS[SETTING_COUNT] = {};

void DeviceSettingsView::initDefs() {
  static const char* sleepTimeoutValues[5];
  static const char* startupValues[2];
  static const char* shortPwrValues[4];
  static const char* toggleValues[2];
  static const char* frontButtonValues[2];
  static const char* sideButtonValues[2];

  sleepTimeoutValues[0] = tr(MIN_5);
  sleepTimeoutValues[1] = tr(MIN_10);
  sleepTimeoutValues[2] = tr(MIN_15);
  sleepTimeoutValues[3] = tr(MIN_30);
  sleepTimeoutValues[4] = tr(NEVER);
  startupValues[0] = tr(LAST_DOCUMENT);
  startupValues[1] = tr(HOME);
  shortPwrValues[0] = tr(IGNORE);
  shortPwrValues[1] = tr(SLEEP_VAL);
  shortPwrValues[2] = tr(PAGE_TURN);
  shortPwrValues[3] = tr(BOOKMARK_VAL);
  toggleValues[0] = tr(OFF);
  toggleValues[1] = tr(ON);
  frontButtonValues[0] = tr(FRONT_BCLR);
  frontButtonValues[1] = tr(FRONT_LRBC);
  sideButtonValues[0] = tr(PREV_NEXT);
  sideButtonValues[1] = tr(NEXT_PREV);

  DEFS[0] = {tr(FRONT_BUTTONS), frontButtonValues, 2};
  DEFS[1] = {tr(SIDE_BUTTONS), sideButtonValues, 2};
  DEFS[2] = {tr(SHORT_POWER_BUTTON), shortPwrValues, 4};
  DEFS[3] = {tr(STARTUP_BEHAVIOR), startupValues, 2};
  DEFS[4] = {tr(SHOW_RECENTS), toggleValues, 2};
  DEFS[5] = {tr(AUTO_SLEEP_TIMEOUT), sleepTimeoutValues, 5};
  DEFS[6] = {tr(RECYCLE_BIN), toggleValues, 2};
}

// Render functions

void render(const GfxRenderer& r, const Theme& t, const SettingsMenuView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(SETTINGS));

  const char* items[] = {tr(READER), tr(SCREEN), tr(DEVICE), tr(CLEANUP), tr(FIRMWARE_UPDATE), tr(SYSTEM_INFO)};
  static_assert(std::size(items) == SettingsMenuView::ITEM_COUNT);
  const int startY = SettingsListHit::LIST_START_Y;
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

  const char* items[] = {tr(CLEAR_BOOK_CACHE), tr(CLEAR_RECENT), tr(EMPTY_TRASH), tr(CLEAR_DEVICE_STORAGE),
                         tr(FACTORY_RESET)};
  static_assert(std::size(items) == CleanupMenuView::ITEM_COUNT);
  const int startY = SettingsListHit::LIST_START_Y;
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
  const int startY = SettingsListHit::LIST_START_Y;

  for (size_t i = 0; i < SystemInfoView::FIELD_COUNT; ++i) {
    const int y = startY + static_cast<int>(i) * lineHeight;
    twoColumnRow(r, t, y, v.fields[i].label, v.fields[i].value);
  }

  ButtonBar btns{tr(BACK), "", "", ""};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ReaderSettingsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(READER_SETTINGS));

  const int startY = SettingsListHit::LIST_START_Y;
  for (int i = 0; i < v.visibleCount; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    const int index = v.settingIndex(i);
    const auto& def = ReaderSettingsView::DEFS[index];

    enumValue(r, t, y, def.label, v.getCurrentValueStr(index), i == v.selected);
  }

  ButtonBar btns{tr(BACK), "", "<", ">"};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ScreenSettingsView& v) {
  r.clearScreen(t.backgroundColor);
  title(r, t, t.screenMarginTop, tr(SCREEN_SETTINGS));
  const int startY = SettingsListHit::LIST_START_Y;
  for (int i = 0; i < v.visibleCount; i++) {
    const int index = v.settingIndex(i);
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    const auto& def = ScreenSettingsView::DEFS[index];
    if (index == 1 || index == 2) {
      char value[8];
      snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(v.values[index]));
      enumValue(r, t, y, def.label, value, i == v.selected);
    } else {
      enumValue(r, t, y, def.label, v.getCurrentValueStr(index), i == v.selected);
    }
  }
  ButtonBar btns{tr(BACK), "", "<", ">"};
  buttonBar(r, t, btns);
  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const DeviceSettingsView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(DEVICE_SETTINGS));

  const int startY = SettingsListHit::LIST_START_Y;
  menuItem(r, t, startY, tr(WIFI), v.selected == 0);
  for (int i = 1; i < v.visibleCount; i++) {
    const int index = v.settingIndex(i);
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    enumValue(r, t, y, DeviceSettingsView::DEFS[index].label, v.getCurrentValueStr(index), i == v.selected);
  }

  ButtonBar btns{tr(BACK), v.selected == 0 ? tr(OPEN) : "", v.selected == 0 ? "" : "<", v.selected == 0 ? "" : ">"};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

touch::DialogLayout confirmDialogBounds(const GfxRenderer& r, const Theme& t, const ConfirmDialogView& v) {
  const int maxTextWidth = r.getScreenWidth() - 2 * (t.screenMarginSide + t.itemPaddingX);
  int messageLines = std::max(
      1,
      static_cast<int>(
          r.wrapTextWithHyphenation(t.uiFontId, v.line1, maxTextWidth, ConfirmDialogView::MAX_MESSAGE_LINES).size()));
  if (v.line2[0] != '\0') {
    messageLines += std::max(
        1,
        static_cast<int>(
            r.wrapTextWithHyphenation(t.uiFontId, v.line2, maxTextWidth, ConfirmDialogView::MAX_MESSAGE_LINES).size()));
  }
  return confirmDialogLayout(r.getScreenWidth(), r.getScreenHeight(), r.getLineHeight(t.uiFontId), messageLines);
}

void render(const GfxRenderer& r, const Theme& t, const ConfirmDialogView& v) {
  const int pageWidth = r.getScreenWidth();
  const int pageHeight = r.getScreenHeight();
  const int lineHeight = r.getLineHeight(t.uiFontId);
  const int top = (pageHeight - lineHeight * 3) / 2;

  r.clearScreen(t.backgroundColor);

  const int maxTextWidth = pageWidth - 2 * (t.screenMarginSide + t.itemPaddingX);
  const int titleLineHeight = r.getLineHeight(t.readerFontId);
  const bool titleWraps = r.getTextWidth(t.readerFontId, v.title, EpdFontFamily::BOLD) > maxTextWidth;
  const int titleY = top - 40 - (titleWraps ? titleLineHeight : 0);
  centeredTextWrapped(r, t.readerFontId, titleY, v.title, maxTextWidth, ConfirmDialogView::MAX_TITLE_LINES,
                      t.primaryTextBlack, EpdFontFamily::BOLD);

  const int line1Count = centeredTextWrapped(r, t.uiFontId, top, v.line1, maxTextWidth,
                                             ConfirmDialogView::MAX_MESSAGE_LINES, t.primaryTextBlack);
  int messageLines = std::max(1, line1Count);
  if (v.line2[0] != '\0') {
    const int line2Count = centeredTextWrapped(r, t.uiFontId, top + messageLines * lineHeight, v.line2, maxTextWidth,
                                               ConfirmDialogView::MAX_MESSAGE_LINES, t.primaryTextBlack);
    messageLines += std::max(1, line2Count);
  }

  const touch::DialogLayout layout = confirmDialogBounds(r, t, v);
  const char* buttonLabels[] = {tr(YES), tr(NO)};

  for (int i = 0; i < 2; i++) {
    const bool isSelected = (v.selection == i);
    const touch::Rect bounds = layout.choices[i];

    if (isSelected) {
      r.fillRect(bounds.x, bounds.y, bounds.width, bounds.height, t.selectionFillBlack);
    } else {
      r.drawRect(bounds.x, bounds.y, bounds.width, bounds.height, t.primaryTextBlack);
    }

    const bool textColor = isSelected ? t.selectionTextBlack : t.primaryTextBlack;
    const int textWidth = r.getTextWidth(t.uiFontId, buttonLabels[i]);
    const int textX = bounds.x + (bounds.width - textWidth) / 2;
    const int textY = bounds.y + (bounds.height - r.getFontAscenderSize(t.uiFontId)) / 2;
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
  const int startY = SettingsListHit::LIST_START_Y;

  const int marginX = t.screenMarginSide + t.itemPaddingX;
  const int maxTextWidth = r.getScreenWidth() - marginX * 2;
  auto warningLines = r.wrapTextWithHyphenation(t.uiFontId, tr(FIRMWARE_WARNING), maxTextWidth, 2);
  for (size_t i = 0; i < warningLines.size(); i++) {
    r.drawText(t.uiFontId, marginX, startY + lineHeight * (1 + static_cast<int>(i)), warningLines[i].c_str(),
               t.primaryTextBlack);
  }

  const int statusY = startY + lineHeight * 3;
  const int statusLines = centeredTextWrapped(r, t.uiFontId, statusY, v.statusLine, maxTextWidth,
                                              FirmwareUpdateView::MAX_STATUS_LINES, t.primaryTextBlack);
  const int statusLineCount = std::max(1, statusLines);

  if (v.state == FirmwareUpdateView::State::Flashing) {
    progress(r, t, statusY + statusLineCount * lineHeight, v.progressPercent, 100);
  }

  if (v.state == FirmwareUpdateView::State::Error) {
    centeredText(r, t, statusY + (statusLineCount + 1) * lineHeight, tr(PRESS_ANY_BUTTON));
  }

  bool interactive = v.state == FirmwareUpdateView::State::Idle || v.state == FirmwareUpdateView::State::Error;
  ButtonBar btns{interactive ? tr(BACK) : "", interactive ? tr(RUN) : "", "", ""};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

}  // namespace ui
