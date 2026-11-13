#include "LightsOutView.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "../Elements.h"

namespace ui {
LightsOutLayout lightsOutLayout(int16_t width, int16_t height, bool hasTouch) {
  constexpr int top = 112;
  const int cell = std::max(0, std::min(int(width) - 40, int(height) - top - 96) / papyrix::lightsout::kSide);
  const int side = cell * papyrix::lightsout::kSide;
  LightsOutLayout layout{};
  layout.board = {int16_t((width - side) / 2), int16_t(top), int16_t(side), int16_t(side)};
  layout.cell = cell;
  layout.hintY = top + side + 8;
  if (hasTouch) {
    const int gap = 10;
    const int buttonWidth = (width - 4 * gap) / 3;
    const int y = height - 56;
    layout.back = {int16_t(gap), int16_t(y), int16_t(buttonWidth), 46};
    layout.menu = {int16_t(2 * gap + buttonWidth), int16_t(y), int16_t(buttonWidth), 46};
    layout.toggle = {int16_t(3 * gap + 2 * buttonWidth), int16_t(y), int16_t(buttonWidth), 46};
  }
  return layout;
}

int lightsOutCellAt(const LightsOutLayout& layout, touch::Point point) {
  if (!layout.board.contains(point) || layout.cell <= 0) return -1;
  return (point.y - layout.board.y) / layout.cell * papyrix::lightsout::kSide +
         (point.x - layout.board.x) / layout.cell;
}

void renderLightsOut(const GfxRenderer& r, const Theme& theme, const papyrix::lightsout::Game& game, bool hasTouch) {
  const auto layout = lightsOutLayout(r.getScreenWidth(), r.getScreenHeight(), hasTouch);
  const bool won = papyrix::lightsout::isWon(game);
  r.clearScreen(theme.backgroundColor);
  title(r, theme, theme.screenMarginTop, tr(LIGHTS_OUT));
  char moves[64];
  std::snprintf(moves, sizeof(moves), "%s: %lu", tr(GAME_MOVES), static_cast<unsigned long>(game.moves));
  r.drawCenteredText(theme.uiFontId, 70, moves, theme.primaryTextBlack);
  for (int i = 0; i < papyrix::lightsout::kCells; ++i) {
    const int x = layout.board.x + (i % papyrix::lightsout::kSide) * layout.cell;
    const int y = layout.board.y + (i / papyrix::lightsout::kSide) * layout.cell;
    r.drawRect(x + 5, y + 5, layout.cell - 10, layout.cell - 10, theme.primaryTextBlack);
    if (game.lights & (uint32_t{1} << i))
      r.fillRect(x + 10, y + 10, layout.cell - 20, layout.cell - 20, theme.primaryTextBlack);
    if (i == game.cursor) {
      r.drawRect(x + 2, y + 2, layout.cell - 4, layout.cell - 4, theme.primaryTextBlack);
      r.drawRect(x + 3, y + 3, layout.cell - 6, layout.cell - 6, theme.primaryTextBlack);
    }
  }
  r.drawCenteredText(theme.smallFontId, layout.hintY,
                     won        ? tr(LIGHTS_OUT_SOLVED)
                     : hasTouch ? tr(LIGHTS_OUT_TOUCH_HINT)
                                : tr(HOLD_CONFIRM_MENU),
                     theme.primaryTextBlack);
  if (hasTouch) {
    const touch::Rect controls[] = {layout.back, layout.menu, layout.toggle};
    const char* labels[] = {tr(BACK), tr(MENU), won ? tr(GAME_NEW) : tr(LIGHTS_OUT_TOGGLE)};
    for (int i = 0; i < 3; ++i) {
      const auto& rect = controls[i];
      r.drawRect(rect.x, rect.y, rect.width, rect.height, theme.primaryTextBlack);
      const std::string label = r.truncatedText(theme.smallFontId, labels[i], rect.width - 12);
      const int textWidth = r.getTextWidth(theme.smallFontId, label.c_str());
      r.drawText(theme.smallFontId, rect.x + (rect.width - textWidth) / 2,
                 rect.y + (rect.height - r.getLineHeight(theme.smallFontId)) / 2, label.c_str(),
                 theme.primaryTextBlack);
    }
  } else {
    buttonBar(r, theme, ButtonBar(tr(BACK), won ? tr(GAME_NEW) : tr(LIGHTS_OUT_TOGGLE), "<", ">"));
  }
}
}  // namespace ui
