#include "test_utils.h"
#include "ui/views/CalibreViews.h"
#include "ui/views/NetworkViews.h"
#include "ui/views/SettingsViews.h"
#include "ui/views/UtilityViews.h"

int main() {
  TestUtils::TestRunner r("Service views touch");

  const ui::SettingsListLayout categories{5};
  auto setting = categories.hitTest({20, 100}, 800, 480, 40);
  r.expectTrue(setting.type == ui::SettingsListHit::Type::Row && setting.index == 1, "settings category row");
  setting = categories.hitTest({20, 500}, 800, 480, 40);
  r.expectTrue(setting.type == ui::SettingsListHit::Type::None, "settings rejects hidden row");
  r.expectTrue(categories.hitTest({438, 430}, 800, 480, 40).type == ui::SettingsListHit::Type::Previous,
               "settings previous value action");
  r.expectTrue(categories.hitTest({619, 430}, 800, 480, 40).type == ui::SettingsListHit::Type::Next,
               "settings next value action");

  auto checkLayout = [&](const ui::SettingsListLayout& layout) {
    for (const int height : {480, 528, 792, 800}) {
      Theme theme = getBuiltinLightTheme();
      theme.itemHeight = 75;
      const int pitch = layout.rowHeight(theme, height);
      int bottom = ui::SettingsListHit::LIST_START_Y;
      for (int row = 0; row < layout.rowCount; ++row) {
        const int y = layout.rowY(row, pitch);
        r.expectTrue(y >= bottom, "settings rows do not overlap");
        for (const int edge : {y, y + pitch - 1}) {
          const auto hit = layout.hitTest({20, static_cast<int16_t>(edge)}, 480, height, pitch);
          r.expectTrue(hit.type == ui::SettingsListHit::Type::Row && hit.index == row,
                       "both row edges select the displayed setting");
        }
        if (y > bottom) {
          r.expectTrue(layout.hitTest({20, static_cast<int16_t>(bottom)}, 480, height, pitch).type ==
                               ui::SettingsListHit::Type::None &&
                           layout.hitTest({20, static_cast<int16_t>(y - 1)}, 480, height, pitch).type ==
                               ui::SettingsListHit::Type::None,
                       "group gaps do not select an adjacent setting");
        }
        bottom = y + pitch;
      }
      r.expectTrue(bottom <= height - ui::SettingsListHit::LIST_BOTTOM_MARGIN,
                   "settings groups fit above the button bar");
      r.expectTrue(layout.hitTest({20, static_cast<int16_t>(bottom)}, 480, height, pitch).type ==
                       ui::SettingsListHit::Type::None,
                   "settings reject touches below the last row");
    }
  };

  ui::ScreenSettingsView light;
  light.frontLightAvailable = true;
  light.selected = 3;
  r.expectEq(uint8_t{0}, light.adjustedLightValue(-1), "brightness cannot wrap below off");
  light.values[1] = 98;
  r.expectEq(uint8_t{100}, light.adjustedLightValue(1), "brightness stops at full output");
  light.selected = 4;
  light.values[2] = 2;
  r.expectEq(uint8_t{0}, light.adjustedLightValue(-1), "warmth stops at the cold channel");
  light.values[2] = 100;
  r.expectEq(uint8_t{100}, light.adjustedLightValue(1), "warmth cannot wrap past the warm channel");
  r.expectEq(uint8_t{95}, light.adjustedLightValue(-1), "warmth changes in five percent steps");
  ui::ScreenSettingsView lit;
  for (const bool frontLight : {false, true}) {
    lit.frontLightAvailable = frontLight;
    for (const auto sleepMode : {papyrix::Settings::SleepDark, papyrix::Settings::SleepCustom}) {
      lit.values[9] = sleepMode;
      lit.selected = 0;
      lit.moveUp();
      r.expectTrue(lit.sleepImageSelected() == (sleepMode == papyrix::Settings::SleepCustom),
                   "image picker is reachable only in Custom mode");
      lit.moveDown();
      r.expectEq(int8_t{0}, lit.selected, "screen wraps from its last visible setting");
      checkLayout(lit.layout());
    }
  }
  ui::ReaderSettingsView reader;
  reader.selected = 6;
  reader.moveDown();
  r.expectTrue(reader.settingIndex(reader.selected) != 7, "button-only Reader skips touch page turns");
  for (const bool touch : {false, true}) {
    reader.visibleCount = ui::ReaderSettingsView::SETTING_COUNT - (touch ? 0 : 1);
    checkLayout(reader.layout());
  }
  ui::DeviceSettingsView device;
  r.expectEq(int8_t{0}, device.selected, "Device opens with Wi-Fi action selected");
  device.moveDown();
  r.expectEq(0, device.settingIndex(device.selected), "First preference remains front buttons");
  device.moveUp();
  r.expectEq(-1, device.settingIndex(device.selected), "Wi-Fi action has no stored setting value");
  for (const bool batch : {false, true}) {
    device.visibleCount = ui::DeviceSettingsView::SETTING_COUNT + (batch ? 1 : 0);
    checkLayout(device.layout());
  }
  ui::WifiMenuView saved;
  saved.count = 9;
  saved.selected = 8;
  auto savedHit = saved.hitTest({20, 300}, 800, 480, 30);
  r.expectTrue(savedHit.type == ui::WifiMenuView::Hit::Row && savedHit.index == 8,
               "add-network row accepts touch after eight saved networks");
  savedHit = saved.hitTest({20, 360}, 800, 480, 30);
  r.expectTrue(savedHit.type == ui::WifiMenuView::Hit::None, "saved menu ignores rows beyond its count");
  saved.buttons = ui::ButtonBar{"Back", "Open", "<", ">"};
  const auto up = saved.hitTest({438, 430}, 800, 480, 30);
  const auto down = saved.hitTest({619, 430}, 800, 480, 30);
  r.expectTrue(up.type == ui::WifiMenuView::Hit::MoveUp && down.type == ui::WifiMenuView::Hit::MoveDown,
               "saved menu exposes distinct reorder controls");
  r.expectTrue(saved.hitTest({75, 430}, 800, 480, 30, true).type == up.type &&
                   saved.hitTest({256, 430}, 800, 480, 30, true).type == down.type,
               "reorder controls follow front-button layout");
  saved.buttons = ui::ButtonBar{"Back", "Open"};
  r.expectTrue(saved.hitTest({438, 430}, 800, 480, 30).type == ui::WifiMenuView::Hit::None &&
                   saved.hitTest({619, 430}, 800, 480, 30).type == ui::WifiMenuView::Hit::None,
               "network actions do not expose reorder controls");

  ui::NetworkModeView modes;
  modes.itemCount = 3;
  auto mode = modes.hitTest({20, 160}, 800, 480, 52);
  r.expectTrue(mode.type == ui::NetworkModeView::Hit::Type::Row && mode.index == 1, "network mode row");

  ui::WifiListView wifi;
  wifi.networkCount = 12;
  wifi.page = 1;
  auto network = wifi.hitTest({20, 60}, 800, 480, 40);
  r.expectTrue(network.type == ui::WifiListView::Hit::Type::Row && network.index == 10, "Wi-Fi row maps page index");
  wifi.scanning = true;
  r.expectTrue(wifi.hitTest({20, 60}, 800, 480, 40).type == ui::WifiListView::Hit::Type::None,
               "scanning list rejects rows");

  ui::KeyboardView keyboard;
  auto key = keyboard.hitTest({760, 410}, 800, 480, 20);
  r.expectTrue(key.type == ui::KeyboardView::Hit::Type::Key && key.row == 9 && key.column == 9,
               "password keyboard last edge key");
  key = keyboard.hitTest({590, 128}, 800, 480, 20);
  r.expectTrue(key.type == ui::KeyboardView::Hit::Type::Key && key.row == 0 && key.column == 7,
               "password keyboard confirm edge key");
  ui::KeyboardView password;
  for (int i = 0; i < 64; ++i) password.appendChar('a');
  r.expectEq(uint8_t{64}, password.inputLen, "Wi-Fi keyboard accepts 64-byte passwords");

  ui::ConfirmView confirm;
  const auto dialog = ui::touch::confirmationDialogLayout(800, 480, 160);
  r.expectTrue(
      confirm.hitTest({dialog.choices[0].x, dialog.choices[0].y}, dialog, 800, 480) == ui::ConfirmView::Hit::Yes,
      "utility confirmation yes");
  r.expectTrue(
      confirm.hitTest({dialog.choices[1].x, dialog.choices[1].y}, dialog, 800, 480) == ui::ConfirmView::Hit::No,
      "utility confirmation no");
  r.expectTrue(confirm.hitTest({75, 430}, dialog, 800, 480) == ui::ConfirmView::Hit::Back,
               "confirmation Back uses the visible screen button bar");
  r.expectTrue(confirm.hitTest({256, 430}, dialog, 800, 480) == ui::ConfirmView::Hit::Select,
               "confirmation Select uses the visible screen button bar");
  r.expectTrue(confirm.hitTest({75, 110}, dialog, 800, 480) == ui::ConfirmView::Hit::None,
               "blank area above the dialog has no button action");

  ui::WifiConnectingView connecting;
  connecting.status = ui::WifiConnectingView::Status::Failed;
  connecting.buttons = ui::ButtonBar{"Back", "Retry"};
  r.expectTrue(connecting.hitTest({256, 430}, 800, 480) == ui::WifiConnectingView::Hit::Primary,
               "connection retry action");

  ui::CalibreView calibre;
  calibre.status = ui::CalibreView::Status::Complete;
  calibre.showRestartOption = true;
  calibre.buttons = ui::ButtonBar{"Back", "Restart"};
  r.expectTrue(calibre.hitTest({75, 430}, 800, 480) == ui::CalibreView::Hit::Back, "Calibre back action");
  r.expectTrue(calibre.hitTest({256, 430}, 800, 480) == ui::CalibreView::Hit::Restart, "Calibre restart action");

  return r.allPassed() ? 0 : 1;
}
