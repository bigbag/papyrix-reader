#include "test_utils.h"
#include "ui/views/CalibreViews.h"
#include "ui/views/NetworkViews.h"
#include "ui/views/SettingsViews.h"
#include "ui/views/UtilityViews.h"

int main() {
  TestUtils::TestRunner r("Service views touch");

  auto setting = ui::settingsListHitTest({20, 100}, 800, 480, 40, 5);
  r.expectTrue(setting.type == ui::SettingsListHit::Type::Row && setting.index == 1, "settings category row");
  setting = ui::settingsListHitTest({20, 500}, 800, 480, 40, 5);
  r.expectTrue(setting.type == ui::SettingsListHit::Type::None, "settings rejects hidden row");
  r.expectTrue(ui::settingsListHitTest({438, 430}, 800, 480, 40, 12).type == ui::SettingsListHit::Type::Previous,
               "settings previous value action");
  r.expectTrue(ui::settingsListHitTest({619, 430}, 800, 480, 40, 12).type == ui::SettingsListHit::Type::Next,
               "settings next value action");

  ui::ScreenSettingsView light;
  light.visibleCount = ui::ScreenSettingsView::SETTING_COUNT;
  light.selected = 1;
  r.expectEq(uint8_t{0}, light.adjustedLightValue(-1), "brightness cannot wrap below off");
  light.values[1] = 98;
  r.expectEq(uint8_t{100}, light.adjustedLightValue(1), "brightness stops at full output");
  light.selected = 2;
  light.values[2] = 2;
  r.expectEq(uint8_t{0}, light.adjustedLightValue(-1), "warmth stops at the cold channel");
  light.values[2] = 100;
  r.expectEq(uint8_t{100}, light.adjustedLightValue(1), "warmth cannot wrap past the warm channel");
  r.expectEq(uint8_t{95}, light.adjustedLightValue(-1), "warmth changes in five percent steps");
  ui::ScreenSettingsView screen;
  screen.moveDown();
  r.expectEq(5, screen.settingIndex(screen.selected), "screen skips unavailable front light after theme");
  screen.moveUp();
  r.expectEq(0, screen.settingIndex(screen.selected), "screen returns directly to theme");
  screen.moveUp();
  r.expectEq(9, screen.settingIndex(screen.selected), "screen wraps to sleep screen");
  ui::ScreenSettingsView lit;
  lit.visibleCount = ui::ScreenSettingsView::SETTING_COUNT;
  lit.selected = 3;
  r.expectEq(3, lit.settingIndex(lit.selected), "front light timeout follows warmth");
  r.expectTrue(!lit.lightSelected(), "front light timeout is not a percent control");
  lit.selected = 4;
  r.expectEq(4, lit.settingIndex(lit.selected), "smooth front light follows timeout");
  lit.selected = 9;
  r.expectEq(9, lit.settingIndex(lit.selected), "sleep screen remains the last screen setting");
  lit.moveDown();
  r.expectEq(0, lit.settingIndex(lit.selected), "screen wraps from sleep screen to theme");
  const auto lastRow = ui::settingsListHitTest({20, 335}, 800, 480, 30, ui::ScreenSettingsView::SETTING_COUNT);
  r.expectTrue(lastRow.type == ui::SettingsListHit::Type::Row && lastRow.index == 9,
               "tenth screen row accepts touch in landscape");
  r.expectTrue(ui::settingsListHitTest({20, 365}, 800, 480, 30, ui::ScreenSettingsView::SETTING_COUNT).type ==
                   ui::SettingsListHit::Type::None,
               "screen rejects touch beyond the tenth row");
  for (const int height : {480, 800}) {
    Theme theme = getBuiltinLightTheme();
    theme.itemHeight = 75;
    const int rowHeight = lit.rowHeight(theme, height);
    const auto bottom = static_cast<int16_t>(ui::SettingsListHit::LIST_START_Y + lit.visibleCount * rowHeight - 1);
    const auto hit = ui::settingsListHitTest({20, bottom}, 480, height, rowHeight, lit.visibleCount);
    r.expectTrue(hit.type == ui::SettingsListHit::Type::Row && hit.index == 9,
                 "large theme rows keep the last Screen setting touchable");
    r.expectTrue(bottom < height - ui::SettingsListHit::LIST_BOTTOM_MARGIN,
                 "large theme rows do not overlap the button bar");
    theme.itemHeight = 30;
    theme.itemSpacing = 50;
    r.expectTrue(lit.rowHeight(theme, height) >= theme.itemHeight,
                 "Screen reduces excess spacing before reducing row height");
  }
  ui::ReaderSettingsView reader;
  reader.selected = 6;
  reader.moveDown();
  r.expectEq(8, reader.settingIndex(reader.selected), "reader skips unavailable touch before full processing");
  reader.moveDown();
  r.expectEq(0, reader.settingIndex(reader.selected), "reader wraps after full processing");
  ui::DeviceSettingsView device;
  r.expectEq(int8_t{0}, device.selected, "Device opens with Wi-Fi action selected");
  device.moveDown();
  r.expectEq(0, device.settingIndex(device.selected), "First preference remains front buttons");
  device.moveUp();
  r.expectEq(-1, device.settingIndex(device.selected), "Wi-Fi action has no stored setting value");
  r.expectTrue(ui::settingsListHitTest({20, 60}, 800, 480, 30, device.visibleCount).index == 0,
               "Wi-Fi row accepts touch");
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
