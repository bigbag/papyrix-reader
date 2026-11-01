#include "Game2048View.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <algorithm>
#include <cstdio>

#include "../Elements.h"

namespace ui {
Game2048Layout game2048Layout(int16_t width, int16_t height, bool hasTouch) {
  Game2048Layout layout{};
  const int available = height - (hasTouch ? 300 : 220);
  const int cell = (std::min(width * 9 / 10, available) - 18) / 4;
  const int side = cell * 4 + 18;
  const int top = height * (hasTouch ? 18 : 20) / 100;
  layout.board = {int16_t((width - side) / 2), int16_t(top), int16_t(side), int16_t(side)};
  layout.hintY = top + side + 24;
  if (hasTouch) {
    const int arrowWidth = (side - 36) / 4;
    for (int i = 0; i < 4; ++i)
      layout.arrows[i] = {int16_t(layout.board.x + i * (arrowWidth + 12)), int16_t(layout.hintY + 46),
                          int16_t(arrowWidth), 50};
  }
  return layout;
}

int game2048ArrowAt(const Game2048Layout& layout, touch::Point point) {
  for (int i = 0; i < 4; ++i)
    if (layout.arrows[i].contains(point)) return i;
  return -1;
}

void renderGame2048(const GfxRenderer& r, const Theme& theme, const papyrix::game2048::Game& game, bool hasTouch) {
  const auto layout = game2048Layout(r.getScreenWidth(), r.getScreenHeight(), hasTouch);
  r.clearScreen(theme.backgroundColor);
  title(r, theme, theme.screenMarginTop, "2048");
  char score[64];
  std::snprintf(score, sizeof(score), "%s: %lu", tr(GAME_SCORE), static_cast<unsigned long>(game.score));
  r.drawCenteredText(theme.uiFontId, 70, score, theme.primaryTextBlack);
  const int cell = (layout.board.width - 18) / 4;
  for (int i = 0; i < 16; ++i) {
    const int x = layout.board.x + (i % 4) * (cell + 6);
    const int y = layout.board.y + (i / 4) * (cell + 6);
    const bool high = game.cells[i] >= 6;
    if (high)
      r.fillRect(x, y, cell, cell, theme.selectionFillBlack);
    else
      r.drawRect(x, y, cell, cell, theme.primaryTextBlack);
    if (!game.cells[i] || game.cells[i] > 17) continue;
    char number[8];
    std::snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(uint32_t{1} << game.cells[i]));
    const int fonts[] = {theme.readerFontIdLarge,  theme.readerFontIdMedium, theme.readerFontId,
                         theme.readerFontIdXSmall, theme.uiFontId,           theme.smallFontId};
    int font = theme.smallFontId;
    for (const int candidate : fonts) {
      if (r.getTextWidth(candidate, number, EpdFontFamily::BOLD) <= cell - 12) {
        font = candidate;
        break;
      }
    }
    const int textWidth = r.getTextWidth(font, number, EpdFontFamily::BOLD);
    r.drawText(font, x + (cell - textWidth) / 2, y + (cell - r.getLineHeight(font)) / 2, number,
               high ? theme.selectionTextBlack : theme.primaryTextBlack, EpdFontFamily::BOLD);
  }
  r.drawCenteredText(theme.smallFontId, layout.hintY, hasTouch ? tr(GAME_TOUCH_HINT) : tr(GAME_BUTTON_HINT),
                     theme.primaryTextBlack);
  if (hasTouch) {
    for (int i = 0; i < 4; ++i) {
      const auto& rect = layout.arrows[i];
      r.drawRect(rect.x, rect.y, rect.width, rect.height, theme.primaryTextBlack);
      const int cx = rect.x + rect.width / 2, cy = rect.y + rect.height / 2;
      const int dx = i == 0 ? -1 : i == 3 ? 1 : 0;
      const int dy = i == 1 ? -1 : i == 2 ? 1 : 0;
      for (int offset = -1; offset <= 2; ++offset) {
        const int x = cx + (dy != 0 ? offset : 0);
        const int y = cy + (dx != 0 ? offset : 0);
        r.drawLine(x - dx * 10, y - dy * 10, x + dx * 10, y + dy * 10, theme.primaryTextBlack);
        r.drawLine(x + dx * 10, y + dy * 10, x + dy * 7, y - dx * 7, theme.primaryTextBlack);
        r.drawLine(x + dx * 10, y + dy * 10, x - dy * 7, y + dx * 7, theme.primaryTextBlack);
      }
    }
  }
  buttonBar(r, theme, ButtonBar(tr(BACK), tr(MENU), hasTouch ? "" : "<", hasTouch ? "" : ">"));
}
}  // namespace ui
