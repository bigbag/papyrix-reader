#include <cstring>

#include "content/BookmarkManager.h"
#include "core/Types.h"
#include "hal/Input.h"
#include "states/RecentState.h"
#include "test_utils.h"
#include "ui/views/CalibreViews.h"
#include "ui/views/HomeView.h"
#include "ui/views/ReaderViews.h"
#include "ui/views/SettingsViews.h"

using namespace papyrix;

int main() {
  TestUtils::TestRunner runner("CapacityContractsTest");

  runner.expectTrue(static_cast<size_t>(Button::Count) <= sizeof(uint8_t) * 8, "Button count fits the state bitmask");

  {
    ui::SystemInfoView view;
    for (size_t i = 0; i < ui::SystemInfoView::FIELD_COUNT; ++i) {
      const auto field = static_cast<ui::SystemInfoView::Field>(i);
      runner.expectTrue(view.setField(field, "Label", "Value"), "Every system info slot is writable");
      runner.expectTrue(strcmp(view.fields[i].value, "Value") == 0, "System info value is stored by slot");
    }
    runner.expectFalse(view.setField(ui::SystemInfoView::Field::Count, "Overflow", "Overflow"),
                       "System info sentinel cannot index storage");

    view.clear();
    for (size_t i = 0; i < ui::SystemInfoView::FIELD_COUNT; ++i) {
      runner.expectEq('\0', view.fields[i].label[0], "System info clear removes labels");
      runner.expectEq('\0', view.fields[i].value[0], "System info clear removes values");
    }
  }

  {
    ui::SettingsMenuView view;
    for (int i = 0; i < ui::SettingsMenuView::ITEM_COUNT; ++i) view.moveDown();
    runner.expectEq(int8_t(0), view.selected, "Settings menu wraps after its visible items");
  }

  {
    runner.expectEq(5, ui::CleanupMenuView::ITEM_COUNT, "Cleanup menu exposes five items");
    ui::CleanupMenuView cleanup;
    for (int i = 0; i < 4; ++i) cleanup.moveDown();
    runner.expectEq(int8_t(4), cleanup.selected, "Cleanup reaches fifth item");
    cleanup.moveDown();
    runner.expectEq(int8_t(0), cleanup.selected, "Cleanup wraps after fifth item");
  }

  {
    ui::ReaderMenuView view;
    view.show();
    for (int i = 0; i < ui::ReaderMenuView::ITEM_COUNT; ++i) view.moveDown();
    runner.expectEq(int8_t(0), view.selected, "Reader menu wraps at derived count");
  }

  runner.expectEq(2, ui::ConfirmDialogView::MAX_TITLE_LINES, "confirmation title allows two lines");
  runner.expectEq(2, ui::ConfirmDialogView::MAX_MESSAGE_LINES, "each confirmation message allows two lines");
  runner.expectEq(2, ui::FirmwareUpdateView::MAX_STATUS_LINES, "firmware status allows two lines");
  runner.expectEq(2, ui::CalibreView::MAX_TEXT_LINES, "Calibre fields allow two lines");
  runner.expectEq(2, ui::HomeView::MAX_EMPTY_HINT_LINES, "Home empty hint allows two lines");
  runner.expectEq(static_cast<int>(Button::Left), static_cast<int>(RecentState::FILES_BUTTON),
                  "Books opens Files with Left");
  runner.expectEq(static_cast<int>(Button::Right), static_cast<int>(RecentState::INFO_BUTTON),
                  "Books opens Info with Right");
  constexpr size_t legacyBookmarkStorage = 20 * sizeof(Bookmark) + 20 * 65;
  constexpr size_t bookmarkStorage = BookmarkManager::MAX_BOOKMARKS * sizeof(Bookmark);
  runner.expectEq(50, BookmarkManager::MAX_BOOKMARKS, "Each book supports 50 bookmarks");
  runner.expectTrue(BookmarkManager::MAX_BOOKMARKS <= 255, "Bookmark count fits the binary file count");
  runner.expectTrue(sizeof(ui::BookmarkListView) < sizeof(Bookmark), "Bookmark view does not duplicate label storage");
  runner.expectTrue(bookmarkStorage <= legacyBookmarkStorage + 1024,
                    "Bookmark storage adds no more than 1 KB of permanent RAM");

  return runner.allPassed() ? 0 : 1;
}
