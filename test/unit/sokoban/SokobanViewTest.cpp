#include <vector>

#include "game2048_fonts.h"
#include "test_utils.h"
#include "ui/views/SokobanView.h"

namespace {
papyrix::sokoban::Level testLevel{12, 12, nullptr};

bool overlaps(const ui::touch::Rect& a, const ui::touch::Rect& b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
}  // namespace

namespace papyrix::sokoban {
const Level* level(uint8_t index) { return index == 0 ? &testLevel : nullptr; }
char terrain(const Game&, uint8_t cell) {
  if (cell == 0) return '#';
  return cell == 1 || cell == 3 ? '.' : ' ';
}
bool hasBox(const Game& game, uint8_t cell) {
  for (int i = 0; i < game.boxCount; ++i)
    if (game.boxes[i] == cell) return true;
  return false;
}
bool isWon(const Game& game) { return game.moves == 42; }
}  // namespace papyrix::sokoban

int main() {
  TestUtils::TestRunner runner("Sokoban view");
  papyrix::sokoban::Game game{};
  game.player = 4;
  game.boxCount = 2;
  game.boxes[0] = 2;
  game.boxes[1] = 3;
  testLevel = {7, 6, nullptr};
  const auto portraitBoard = ui::sokobanLayout(480, 800, game, false).board;
  runner.expectTrue(portraitBoard.width >= 440 && portraitBoard.height >= 520 && portraitBoard.height <= 534,
                    "X4 portrait board keeps its width and uses two thirds of screen height");
  for (const auto dimensions : {ui::touch::Point{12, 12}, ui::touch::Point{12, 4}, ui::touch::Point{4, 12}}) {
    testLevel.width = dimensions.x;
    testLevel.height = dimensions.y;
    for (bool touch : {false, true}) {
      for (const auto size : {ui::touch::Point{480, 800}, ui::touch::Point{528, 792}, ui::touch::Point{800, 480},
                              ui::touch::Point{792, 528}}) {
        const auto layout = ui::sokobanLayout(size.x, size.y, game, touch);
        const auto& board = layout.board;
        runner.expectTrue(layout.cellWidth >= 20 && layout.cellHeight >= 20 &&
                              board.width == dimensions.x * layout.cellWidth &&
                              board.height == dimensions.y * layout.cellHeight && board.x >= 0 && board.y >= 100 &&
                              board.x + board.width <= size.x && board.y + board.height < layout.hintY &&
                              layout.hintY + 24 <= size.y - (touch ? 0 : 50),
                          "board and hint fit on screen at maximum dimensions");
        if (!touch || size.x <= size.y) {
          const int horizontalOffset = 2 * board.x + board.width - size.x;
          runner.expectTrue(horizontalOffset >= -1 && horizontalOffset <= 1,
                            "board has equal left and right margins without side controls");
        }
        if (!touch) continue;
        const ui::touch::Rect controls[] = {layout.back,          layout.menu,          layout.undo,
                                            layout.directions[0], layout.directions[1], layout.directions[2],
                                            layout.directions[3]};
        const int hintWidth = size.x > size.y ? size.x - 190 : size.x;
        const ui::touch::Rect hint{0, int16_t(layout.hintY), int16_t(hintWidth), 24};
        for (int i = 0; i < 7; ++i) {
          const auto& control = controls[i];
          runner.expectTrue(control.width >= 46 && control.height >= 46 && control.x >= 0 && control.y >= 0 &&
                                control.x + control.width <= size.x && control.y + control.height <= size.y &&
                                !overlaps(control, board) && !overlaps(control, hint),
                            "touch target fits and stays clear of board and hint");
          for (int j = 0; j < i; ++j)
            runner.expectTrue(!overlaps(control, controls[j]), "touch targets do not overlap");
          runner.expectTrue(
              control.contains({control.x, control.y}) &&
                  control.contains({int16_t(control.x + control.width - 1), int16_t(control.y + control.height - 1)}) &&
                  !control.contains({int16_t(control.x + control.width), control.y}),
              "touch target includes its drawn interior and excludes its right edge");
        }
        runner.expectTrue(
            layout.directions[0].y < layout.directions[2].y && layout.directions[1].y > layout.directions[2].y &&
                layout.directions[2].x < layout.directions[0].x && layout.directions[3].x > layout.directions[0].x,
            "direction order is up, down, left, right");
      }
    }
  }

  testLevel = {12, 12, nullptr};
  for (bool x3 : {false, true}) {
    papyrix::hal::Display panel;
    if (x3) panel.setDisplayX3();
    GfxRenderer renderer(panel);
    renderer.begin();
    game2048Fonts(renderer);
    for (bool landscape : {false, true}) {
      renderer.setOrientation(landscape ? GfxRenderer::LandscapeCounterClockwise : GfxRenderer::Portrait);
      for (bool touch : {false, true}) {
        const auto layout = ui::sokobanLayout(renderer.getScreenWidth(), renderer.getScreenHeight(), game, touch);
        ui::renderSokoban(renderer, getBuiltinLightTheme(), game, touch);
        const std::vector<uint8_t> light(renderer.getFrameBuffer(),
                                         renderer.getFrameBuffer() + renderer.getBufferSize());
        std::vector<std::vector<bool>> tiles(6, std::vector<bool>(layout.cellWidth * layout.cellHeight));
        for (int py = 0; py < panel.getDisplayHeight(); ++py) {
          for (int px = 0; px < panel.getDisplayWidth(); ++px) {
            int x, y;
            if (!renderer.panelToLogical(px, py, &x, &y)) continue;
            if (y < layout.board.y || y >= layout.board.y + layout.cellHeight || x < layout.board.x ||
                x >= layout.board.x + 6 * layout.cellWidth)
              continue;
            const int localX = x - layout.board.x;
            tiles[localX / layout.cellWidth][(y - layout.board.y) * layout.cellWidth + localX % layout.cellWidth] =
                !(light[py * panel.getDisplayWidthBytes() + px / 8] & (0x80 >> (px % 8)));
          }
        }
        for (int i = 0; i < 6; ++i)
          for (int j = 0; j < i; ++j)
            runner.expectTrue(tiles[i] != tiles[j], "wall, target, boxes, player and floor have distinct patterns");
        ui::renderSokoban(renderer, getBuiltinDarkTheme(), game, touch);
        bool inverted = true;
        for (size_t i = 0; i < light.size(); ++i)
          inverted = inverted && renderer.getFrameBuffer()[i] == uint8_t(~light[i]);
        runner.expectTrue(inverted, "dark theme inverts all view pixels");
        game.moves = 42;
        ui::renderSokoban(renderer, getBuiltinLightTheme(), game, touch);
        bool solvedActionChanged = false;
        const auto action =
            touch ? layout.undo : ui::touch::Rect{0, int16_t(layout.hintY), int16_t(renderer.getScreenWidth()), 24};
        for (int py = 0; py < panel.getDisplayHeight(); ++py) {
          for (int px = 0; px < panel.getDisplayWidth(); ++px) {
            int x, y;
            if (!renderer.panelToLogical(px, py, &x, &y) || !action.contains({int16_t(x), int16_t(y)})) continue;
            const size_t offset = py * panel.getDisplayWidthBytes() + px / 8;
            const uint8_t mask = 0x80 >> (px % 8);
            solvedActionChanged = solvedActionChanged || ((renderer.getFrameBuffer()[offset] ^ light[offset]) & mask);
          }
        }
        runner.expectTrue(solvedActionChanged, "solved state changes the action or hint, not only the counters");
        game.moves = 0;
      }
    }
  }
  return runner.allPassed() ? 0 : 1;
}
