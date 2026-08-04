#include "test_utils.h"

#include <cstring>

#include "content/BookmarkManager.h"
#include "core/Types.h"
#include "drivers/Input.h"
#include "states/RecentState.h"
#include "ui/views/ReaderViews.h"
#include "ui/views/SettingsViews.h"

using namespace papyrix;

int main() {
  TestUtils::TestRunner runner("CapacityContractsTest");

  runner.expectTrue(static_cast<size_t>(Button::Count) <= sizeof(uint8_t) * 8,
                    "Button count fits the state bitmask");

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
    runner.expectEq(int8_t(0), view.selected, "Settings menu wraps at derived count");
    runner.expectTrue(ui::SettingsMenuView::Item::SystemInfo != ui::SettingsMenuView::Item::Count,
                      "Settings menu last item is distinct from sentinel");
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
    runner.expectEq(static_cast<int8_t>(ui::ReaderMenuView::ITEM_COUNT - 1), view.selected,
                    "Reader menu clamps at derived count");
  }

  runner.expectEq(3, ui::ReaderMenuView::ITEM_COUNT, "Reader menu exposes three items");
  runner.expectEq(static_cast<int>(Button::Left), static_cast<int>(RecentState::FILES_BUTTON),
                  "Books opens Files with Left");
  runner.expectEq(static_cast<int>(Button::Right), static_cast<int>(RecentState::INFO_BUTTON),
                  "Books opens Info with Right");
  runner.expectEq(BookmarkManager::MAX_BOOKMARKS, ui::BookmarkListView::MAX_ITEMS,
                  "Bookmark manager and view capacities match");

  return runner.allPassed() ? 0 : 1;
}
