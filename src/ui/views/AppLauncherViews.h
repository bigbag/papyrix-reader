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
    const int row =
        touch::rowAt(point, {0, LIST_START_Y, screenWidth, static_cast<int16_t>(screenHeight - LIST_START_Y - 50)},
                     rowHeight, itemCount);
    return row < 0 ? Hit{} : Hit{Hit::Type::Entry, row};
  }
};

void render(const GfxRenderer& r, const Theme& t, const AppMenuView& v, const papyrix::MiniApp* apps);

}  // namespace ui
