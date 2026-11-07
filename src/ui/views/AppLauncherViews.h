#pragma once

#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <cstdint>

#include "../../apps/MiniApp.h"
#include "../Elements.h"

namespace ui {

struct AppMenuView {
  struct Hit {
    enum class Type : uint8_t { None, Entry, Back, Open };
    Type type = Type::None;
    int index = -1;
  };
  static constexpr int LIST_START_Y = 60;
  static constexpr int EXTRA_COUNT = 2;
  static constexpr int GROUP_GAP = 8;

  static int rowY(int index, int rowHeight) {
    int y = LIST_START_Y + index * rowHeight;
    if (index >= 4) y += GROUP_GAP;
    if (index >= 7) y += GROUP_GAP;
    return y;
  }

  int rowPitch(int screenHeight, int requestedPitch) const {
    if (itemCount <= 0 || requestedPitch <= 0) return 0;
    const int gaps = (itemCount > 4 ? GROUP_GAP : 0) + (itemCount > 7 ? GROUP_GAP : 0);
    const int available = screenHeight - 50 - LIST_START_Y - gaps;
    if (available <= 0) return 0;
    const int limit = available / itemCount;
    return requestedPitch < limit ? requestedPitch : limit;
  }

  ButtonBar buttons;
  int8_t selected = 0;
  int8_t appCount = 0;
  int8_t itemCount = 0;
  bool needsRender = true;

  void moveUp() {
    if (itemCount == 0) return;
    selected = (selected == 0) ? itemCount - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    if (itemCount == 0) return;
    selected = (selected + 1) % itemCount;
    needsRender = true;
  }
  Hit hitTest(touch::Point point, int16_t screenWidth, int16_t screenHeight, int16_t rowHeight,
              bool frontLrbc = false) const {
    const int action = touch::semanticButtonBarIndex(point, screenWidth, screenHeight, frontLrbc);
    if (action == 0) return {Hit::Type::Back, -1};
    if (action == 1) return {Hit::Type::Open, -1};
    const touch::Rect list{0, LIST_START_Y, screenWidth, static_cast<int16_t>(screenHeight - LIST_START_Y - 50)};
    rowHeight = rowPitch(screenHeight, rowHeight);
    if (!list.contains(point) || rowHeight <= 0) return {};
    for (int i = 0; i < itemCount; ++i) {
      const int y = rowY(i, rowHeight);
      if (point.y >= y && point.y < y + rowHeight) return {Hit::Type::Entry, i};
    }
    return {};
  }
};

inline int appIdForMenuItem(int index) {
  static const int8_t order[] = {papyrix::APP_LOCALSEND, papyrix::APP_PRINTER, papyrix::APP_IMAGEVIEWER,
                                 papyrix::APP_POMODORO,  papyrix::APP_CLOCK,   papyrix::APP_2048,
                                 papyrix::APP_GO};
  return order[index - AppMenuView::EXTRA_COUNT];
}

void render(const GfxRenderer& r, const Theme& t, const AppMenuView& v, const papyrix::MiniApp* apps);

}  // namespace ui
