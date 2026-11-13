#include "SokobanView.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../Elements.h"

namespace ui {
namespace {
void drawCat(const GfxRenderer& r, int cx, int cy, int cell, bool ink) {
  const int h = cell / 2 - std::max(2, cell / 10);
  const auto x = [&](int u) { return cx + u * h / 16; };
  const auto y = [&](int v) { return cy + v * h / 16; };
  const int eye = std::max(2, h / 6);
  const auto line = [&](int x1, int y1, int x2, int y2) {
    const bool steep = std::abs(x2 - x1) < std::abs(y2 - y1);
    r.drawLine(x1, y1, x2, y2, ink);
    r.drawLine(x1 + steep, y1 + !steep, x2 + steep, y2 + !steep, ink);
  };
  for (int s = -1; s <= 1; s += 2) {
    line(x(s * 15), y(0), x(s * 13), y(-15));
    line(x(s * 13), y(-15), x(s * 5), y(-9));
    line(x(s * 5), y(-9), x(0), y(-10));
    line(x(s * 15), y(0), x(s * 14), y(8));
    line(x(s * 14), y(8), x(s * 8), y(13));
    line(x(s * 8), y(13), x(0), y(15));
    r.fillRect(x(s * 6) - eye / 2, y(-2), eye, 2 * eye, ink);
    line(x(s * 4), y(7), x(s * 19), y(4));
    line(x(s * 4), y(9), x(s * 19), y(12));
    line(x(0), y(10), x(s * 3), y(11));
    if (cell >= 32) line(x(s * 11), y(-10), x(s * 7), y(-6));
  }
  line(x(-2), y(5), x(2), y(5));
  line(x(-2), y(5), x(0), y(7));
  line(x(2), y(5), x(0), y(7));
  line(x(0), y(7), x(0), y(10));
}
}  // namespace

SokobanLayout sokobanLayout(int16_t width, int16_t height, const papyrix::sokoban::Game& game, bool hasTouch) {
  SokobanLayout layout{};
  const auto* level = papyrix::sokoban::level(game.level);
  if (!level || !level->width || !level->height) return layout;

  constexpr int top = 105;
  const bool sideControls = hasTouch && width > height;
  const int availableWidth = sideControls ? width - 190 : width;
  const int boardWidth = availableWidth - (sideControls ? 0 : 20);
  const int bottom = hasTouch && !sideControls ? height - 212 : height - (hasTouch ? 10 : 50);
  const int boardHeight = std::min(int(height) * 2 / 3, bottom - top - 32);
  layout.cellWidth = std::max(0, std::min(boardWidth / level->width, boardHeight / level->height));
  layout.cellHeight = layout.cellWidth;
  const int w = layout.cellWidth * level->width;
  const int h = layout.cellHeight * level->height;
  layout.board = {int16_t((availableWidth - w) / 2), top, int16_t(w), int16_t(h)};
  layout.hintY = top + h + 8;

  if (hasTouch) {
    if (sideControls) {
      const int cx = width - 95;
      layout.directions[0] = {int16_t(cx - 23), 110, 46, 46};
      layout.directions[1] = {int16_t(cx - 23), 214, 46, 46};
      layout.directions[2] = {int16_t(cx - 75), 162, 46, 46};
      layout.directions[3] = {int16_t(cx + 29), 162, 46, 46};
      layout.back = {int16_t(width - 170), int16_t(height - 164), 150, 46};
      layout.menu = {int16_t(width - 170), int16_t(height - 112), 150, 46};
      layout.undo = {int16_t(width - 170), int16_t(height - 60), 150, 46};
    } else {
      const int cx = width / 2;
      layout.directions[0] = {int16_t(cx - 23), int16_t(height - 212), 46, 46};
      layout.directions[1] = {int16_t(cx - 23), int16_t(height - 108), 46, 46};
      layout.directions[2] = {int16_t(cx - 75), int16_t(height - 160), 46, 46};
      layout.directions[3] = {int16_t(cx + 29), int16_t(height - 160), 46, 46};
      const int buttonWidth = (width - 40) / 3;
      layout.back = {10, int16_t(height - 56), int16_t(buttonWidth), 46};
      layout.menu = {int16_t(20 + buttonWidth), int16_t(height - 56), int16_t(buttonWidth), 46};
      layout.undo = {int16_t(30 + 2 * buttonWidth), int16_t(height - 56), int16_t(buttonWidth), 46};
    }
  }
  return layout;
}

void renderSokoban(const GfxRenderer& r, const Theme& theme, const papyrix::sokoban::Game& game, bool hasTouch) {
  using namespace papyrix::sokoban;
  const auto* levelData = level(game.level);
  const auto layout = sokobanLayout(r.getScreenWidth(), r.getScreenHeight(), game, hasTouch);
  const bool won = isWon(game);
  const bool ink = theme.primaryTextBlack;
  r.clearScreen(theme.backgroundColor);
  title(r, theme, theme.screenMarginTop, tr(SOKOBAN));
  char stats[128];
  std::snprintf(stats, sizeof(stats), "%s %u/%u    %s: %lu    %s: %lu", tr(SOKOBAN_LEVEL), unsigned(game.level) + 1,
                unsigned(kLevelCount), tr(GAME_MOVES), static_cast<unsigned long>(game.moves), tr(SOKOBAN_PUSHES),
                static_cast<unsigned long>(game.pushes));
  const int statsWidth = r.getTextWidth(theme.smallFontId, stats);
  if (statsWidth <= r.getScreenWidth() - 20) {
    r.drawText(theme.smallFontId, (r.getScreenWidth() - statsWidth) / 2, 70, stats, ink);
  } else {
    const std::string text = r.truncatedText(theme.smallFontId, stats, r.getScreenWidth() - 20);
    r.drawCenteredText(theme.smallFontId, 70, text.c_str(), ink);
  }

  if (levelData && layout.cellWidth > 0 && layout.cellHeight > 0) {
    const int cell = std::min(layout.cellWidth, layout.cellHeight);
    for (int i = 0; i < levelData->width * levelData->height; ++i) {
      const int x = layout.board.x + (i % levelData->width) * layout.cellWidth;
      const int y = layout.board.y + (i / levelData->width) * layout.cellHeight;
      const int cx = x + layout.cellWidth / 2, cy = y + layout.cellHeight / 2;
      const char ground = terrain(game, uint8_t(i));
      if (ground == '#') {
        r.fillRect(x + 1, y + 1, layout.cellWidth - 2, layout.cellHeight - 2, ink);
      } else if (hasBox(game, uint8_t(i))) {
        const int margin = std::max(2, cell / 8);
        const int side = cell - 2 * margin;
        r.fillRect(cx - side / 2, cy - side / 2, side, side, ink);
        const int d = std::max(2, cell / 4);
        if (ground == '.') {
          r.drawRect(cx - d / 2, cy - d / 2, d, d, !ink);
        } else {
          r.drawLine(cx - d, cy - d, cx + d, cy + d, !ink);
          r.drawLine(cx + d, cy - d, cx - d, cy + d, !ink);
        }
      } else {
        if (ground == '.' && i != game.player) {
          const int d = std::max(2, cell / 4);
          r.drawLine(cx, cy - d, cx + d, cy, ink);
          r.drawLine(cx + d, cy, cx, cy + d, ink);
          r.drawLine(cx, cy + d, cx - d, cy, ink);
          r.drawLine(cx - d, cy, cx, cy - d, ink);
        }
        if (i == game.player) {
          drawCat(r, cx, cy, cell, ink);
          if (ground == '.') r.drawRect(x + 2, y + 2, cell - 4, cell - 4, ink);
        }
      }
    }
  }

  const int hintWidth =
      hasTouch && r.getScreenWidth() > r.getScreenHeight() ? r.getScreenWidth() - 190 : r.getScreenWidth();
  const char* hint = won ? tr(SOKOBAN_SOLVED) : hasTouch ? tr(SOKOBAN_TOUCH_HINT) : tr(SOKOBAN_BUTTON_HINT);
  const int hintTextWidth = r.getTextWidth(theme.smallFontId, hint);
  if (hintTextWidth <= hintWidth - 20) {
    r.drawText(theme.smallFontId, (hintWidth - hintTextWidth) / 2, layout.hintY, hint, ink);
  } else {
    const std::string text = r.truncatedText(theme.smallFontId, hint, hintWidth - 20);
    r.drawText(theme.smallFontId, (hintWidth - r.getTextWidth(theme.smallFontId, text.c_str())) / 2, layout.hintY,
               text.c_str(), ink);
  }
  if (hasTouch) {
    const touch::Rect controls[] = {layout.back, layout.menu, layout.undo};
    const char* labels[] = {tr(BACK), tr(MENU), won ? tr(NEXT) : tr(UNDO)};
    for (int i = 0; i < 3; ++i) {
      const auto& rect = controls[i];
      r.drawRect(rect.x, rect.y, rect.width, rect.height, ink);
      const std::string label = r.truncatedText(theme.smallFontId, labels[i], rect.width - 12);
      r.drawText(theme.smallFontId, rect.x + (rect.width - r.getTextWidth(theme.smallFontId, label.c_str())) / 2,
                 rect.y + (rect.height - r.getLineHeight(theme.smallFontId)) / 2, label.c_str(), ink);
    }
    for (int i = 0; i < 4; ++i) {
      const auto& rect = layout.directions[i];
      r.drawRect(rect.x, rect.y, rect.width, rect.height, ink);
      const int x = rect.x + rect.width / 2, y = rect.y + rect.height / 2;
      const int dx = i == 2 ? -1 : i == 3 ? 1 : 0;
      const int dy = i == 0 ? -1 : i == 1 ? 1 : 0;
      r.drawLine(x - dx * 10, y - dy * 10, x + dx * 10, y + dy * 10, ink);
      r.drawLine(x + dx * 10, y + dy * 10, x + dx * 3 + dy * 7, y + dy * 3 + dx * 7, ink);
      r.drawLine(x + dx * 10, y + dy * 10, x + dx * 3 - dy * 7, y + dy * 3 - dx * 7, ink);
    }
  } else {
    buttonBar(r, theme, ButtonBar(tr(BACK), won ? tr(NEXT) : tr(UNDO), "<", ">"));
  }
}
}  // namespace ui
