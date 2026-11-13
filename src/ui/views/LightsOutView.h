#pragma once

#include "../../apps/LightsOut.h"
#include "../TouchLayout.h"

class GfxRenderer;
struct Theme;

namespace ui {
struct LightsOutLayout {
  touch::Rect board;
  int cell;
  int hintY;
  touch::Rect back;
  touch::Rect menu;
  touch::Rect toggle;
};
LightsOutLayout lightsOutLayout(int16_t width, int16_t height, bool hasTouch);
int lightsOutCellAt(const LightsOutLayout& layout, touch::Point point);
void renderLightsOut(const GfxRenderer& r, const Theme& theme, const papyrix::lightsout::Game& game, bool hasTouch);
}  // namespace ui
