#pragma once

#include "../../apps/Sokoban.h"
#include "../TouchLayout.h"

class GfxRenderer;
struct Theme;

namespace ui {
struct SokobanLayout {
  touch::Rect board;
  int cellWidth;
  int cellHeight;
  int hintY;
  touch::Rect back;
  touch::Rect menu;
  touch::Rect undo;
  touch::Rect directions[4];
};
SokobanLayout sokobanLayout(int16_t width, int16_t height, const papyrix::sokoban::Game& game, bool hasTouch);
void renderSokoban(const GfxRenderer& r, const Theme& theme, const papyrix::sokoban::Game& game, bool hasTouch);
}  // namespace ui
