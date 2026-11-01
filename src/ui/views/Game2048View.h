#pragma once

#include "../../apps/Game2048.h"
#include "../TouchLayout.h"

class GfxRenderer;
struct Theme;

namespace ui {
struct Game2048Layout {
  touch::Rect board;
  touch::Rect arrows[4];
  int hintY;
};
Game2048Layout game2048Layout(int16_t width, int16_t height, bool hasTouch);
int game2048ArrowAt(const Game2048Layout& layout, touch::Point point);
void renderGame2048(const GfxRenderer& r, const Theme& theme, const papyrix::game2048::Game& game, bool hasTouch);
}  // namespace ui
