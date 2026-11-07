#include "GoView.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>

#include "../../apps/ClockFacePrimitives.h"
#include "../Elements.h"

namespace ui {
GoLayout goLayout(int16_t width, int16_t height, bool hasTouch) {
  GoLayout layout{};
  layout.spacing = std::max(1, std::min((width - 32) / 9, (height - 300) / 9));
  const int side = layout.spacing * 9;
  layout.board = {int16_t((width - side) / 2), 150, int16_t(side), int16_t(side)};
  layout.hintY = layout.board.y + side + 12;
  if (hasTouch) {
    const int gap = 10;
    const int buttonWidth = (width - 4 * gap) / 3;
    const int y = height - 56;
    layout.back = {int16_t(gap), int16_t(y), int16_t(buttonWidth), 46};
    layout.menu = {int16_t(2 * gap + buttonWidth), int16_t(y), int16_t(buttonWidth), 46};
    layout.place = {int16_t(3 * gap + 2 * buttonWidth), int16_t(y), int16_t(buttonWidth), 46};
  }
  return layout;
}

int goPointAt(const GoLayout& layout, touch::Point point) {
  if (!layout.board.contains(point) || layout.spacing <= 0) return -1;
  const int x = (point.x - layout.board.x) / layout.spacing;
  const int y = (point.y - layout.board.y) / layout.spacing;
  return y * gogame::kSide + x;
}

void renderGo(const GfxRenderer& r, const Theme& theme, const gogame::Position& position, gogame::Move cursor,
              bool hasTouch, bool thinking, const char* notice) {
  using namespace papyrix::clock_face_primitives;
  const GoLayout layout = goLayout(r.getScreenWidth(), r.getScreenHeight(), hasTouch);
  r.clearScreen(theme.backgroundColor);
  title(r, theme, theme.screenMarginTop, "Go");
  char text[128];
  if (position.outcome == gogame::Outcome::Playing) {
    std::snprintf(text, sizeof(text), "%s: %s%s%s", tr(GO_TURN),
                  position.toMove == gogame::Color::Black ? tr(GO_BLACK) : tr(GO_WHITE), thinking ? " - " : "",
                  thinking ? tr(GO_THINKING) : "");
    r.drawCenteredText(theme.smallFontId, 65, text, theme.primaryTextBlack);
    if (position.lastMove == gogame::kPass)
      r.drawCenteredText(theme.smallFontId, 95, tr(GO_LAST_PASS), theme.primaryTextBlack);
  } else {
    const gogame::Color winner = gogame::winner(position);
    if (winner == gogame::Color::Empty)
      std::snprintf(text, sizeof(text), "%s", tr(GO_DRAW));
    else
      std::snprintf(text, sizeof(text), "%s %s", winner == gogame::Color::Black ? tr(GO_BLACK) : tr(GO_WHITE),
                    tr(GO_WINS));
    r.drawCenteredText(theme.smallFontId, 65, text, theme.primaryTextBlack);
    if (position.outcome == gogame::Outcome::Scored) {
      const gogame::Score score = gogame::score(position);
      std::snprintf(text, sizeof(text), "%s: %d.%d", tr(GO_BLACK), score.blackHalf / 2, score.blackHalf % 2 * 5);
      r.drawCenteredText(theme.smallFontId, 95, text, theme.primaryTextBlack);
      std::snprintf(text, sizeof(text), "%s: %d.%d (%s 7.5)", tr(GO_WHITE), score.whiteHalf / 2,
                    score.whiteHalf % 2 * 5, tr(GO_KOMI));
      r.drawCenteredText(theme.smallFontId, 120, text, theme.primaryTextBlack);
    }
  }
  const int step = layout.spacing;
  const int firstX = layout.board.x + step / 2;
  const int firstY = layout.board.y + step / 2;
  const int lastX = firstX + 8 * step;
  const int lastY = firstY + 8 * step;
  for (int i = 0; i < gogame::kSide; ++i) {
    r.drawLine(firstX + i * step, firstY, firstX + i * step, lastY, theme.primaryTextBlack);
    r.drawLine(firstX, firstY + i * step, lastX, firstY + i * step, theme.primaryTextBlack);
  }
  const int radius = step * 2 / 5;
  for (int i = 0; i < gogame::kPoints; ++i) {
    const int x = firstX + (i % gogame::kSide) * step;
    const int y = firstY + (i / gogame::kSide) * step;
    const auto stone = position.cells[i];
    if (stone != gogame::Color::Empty) {
      fillCircle(r, x, y, radius, stone == gogame::Color::Black);
      drawCircle(r, x, y, radius, 2, theme.primaryTextBlack);
    }
    if (i == position.lastMove) {
      drawCircle(r, x, y, std::max(3, radius / 3), 2,
                 stone == gogame::Color::Black   ? false
                 : stone == gogame::Color::White ? true
                                                 : theme.primaryTextBlack);
    }
    if (i == cursor && position.outcome == gogame::Outcome::Playing) {
      const int d = step / 2 - 2;
      const int arm = std::max(3, step / 6);
      for (int sx : {-1, 1})
        for (int sy : {-1, 1}) {
          r.drawLine(x + sx * d, y + sy * d, x + sx * (d - arm), y + sy * d, theme.primaryTextBlack);
          r.drawLine(x + sx * d, y + sy * d, x + sx * d, y + sy * (d - arm), theme.primaryTextBlack);
        }
    }
  }
  const char* hint = notice && *notice ? notice
                     : position.outcome != gogame::Outcome::Playing
                         ? (position.lastMove == gogame::kPass ? tr(GO_LAST_PASS) : tr(GAME_OVER))
                         : (hasTouch ? tr(GO_TOUCH_HINT) : tr(GO_BUTTON_HINT));
  r.drawCenteredText(theme.smallFontId, layout.hintY, hint, theme.primaryTextBlack);
  if (hasTouch) {
    const touch::Rect controls[] = {layout.back, layout.menu, layout.place};
    const char* labels[] = {tr(BACK), tr(MENU),
                            position.outcome == gogame::Outcome::Playing ? tr(GO_PLACE) : tr(GAME_NEW)};
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
    buttonBar(
        r, theme,
        ButtonBar(tr(BACK), position.outcome == gogame::Outcome::Playing ? tr(GO_PLACE) : tr(GAME_NEW), "<", ">"));
  }
}
}  // namespace ui
