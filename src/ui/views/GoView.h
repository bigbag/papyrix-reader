#pragma once

#include <GoGame.h>

#include "../TouchLayout.h"

class GfxRenderer;
struct Theme;

namespace ui {
struct GoLayout {
  touch::Rect board;
  int spacing;
  int hintY;
  touch::Rect place;
  touch::Rect back;
  touch::Rect menu;
};

GoLayout goLayout(int16_t width, int16_t height, bool hasTouch);
int goPointAt(const GoLayout& layout, touch::Point point);
void renderGo(const GfxRenderer& renderer, const Theme& theme, const gogame::Position& position, gogame::Move cursor,
              bool hasTouch, bool thinking, const char* notice);
}  // namespace ui
