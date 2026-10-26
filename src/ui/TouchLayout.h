#pragma once
#include <cstdint>

namespace ui::touch {
struct Point {
  int16_t x;
  int16_t y;
};
struct Rect {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
  bool contains(Point point) const;
};
struct DialogLayout {
  Rect bounds;
  Rect choices[2];
};
enum class ReaderRegion : uint8_t { None, Previous, Menu, Next, Back, Confirm };
inline constexpr int16_t READER_BOTTOM_ZONE_HEIGHT = 50;
ReaderRegion readerRegionAt(Point point, int16_t screenWidth, int16_t screenHeight);
int rowAt(Point point, Rect bounds, int16_t rowHeight, int rowCount);
int clippedRowAt(Point point, Rect bounds, int16_t rowHeight, int rowCount, int firstRow);
int gridIndexAt(Point point, Rect bounds, int columns, int rows);
int pagedRowAt(Point point, Rect bounds, int16_t rowHeight, int itemCount, int page, int rowsPerPage);
Rect buttonBarButtonRect(int index, int16_t screenWidth, int16_t screenHeight, int16_t buttonWidth = 106);
int buttonBarIndex(Point point, int16_t screenWidth, int16_t screenHeight, int16_t buttonWidth = 106);
int semanticButtonBarIndex(Point point, int16_t screenWidth, int16_t screenHeight, bool frontLrbc);
DialogLayout confirmationDialogLayout(int16_t screenWidth, int16_t screenHeight, int16_t dialogHeight);
int dialogChoiceAt(Point point, const DialogLayout& layout);
int popupMenuRowAt(Point point, Rect rows, int16_t rowHeight, int itemCount);
int keyboardIndexAt(Point point, Rect grid, int columns, int rows);
}  // namespace ui::touch
