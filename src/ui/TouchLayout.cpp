#include "TouchLayout.h"

namespace ui::touch {
bool Rect::contains(Point p) const {
  return width > 0 && height > 0 && p.x >= x && p.y >= y && p.x < x + width && p.y < y + height;
}

ReaderRegion readerRegionAt(Point point, int16_t screenWidth, int16_t screenHeight) {
  if (screenWidth <= 0 || screenHeight <= READER_BOTTOM_ZONE_HEIGHT || point.x < 0 || point.y < 0 ||
      point.x >= screenWidth || point.y >= screenHeight) {
    return ReaderRegion::None;
  }
  const int16_t edgeWidth = static_cast<int16_t>(static_cast<int32_t>(screenWidth) * 24 / 100);
  if (point.y >= screenHeight - READER_BOTTOM_ZONE_HEIGHT) {
    if (point.x < edgeWidth) return ReaderRegion::Back;
    if (point.x >= screenWidth - edgeWidth) return ReaderRegion::Confirm;
    return ReaderRegion::None;
  }
  if (point.x < edgeWidth) return ReaderRegion::Previous;
  if (point.x >= screenWidth - edgeWidth) return ReaderRegion::Next;
  return ReaderRegion::Menu;
}

int rowAt(Point p, Rect bounds, int16_t rowHeight, int rowCount) {
  if (!bounds.contains(p) || rowHeight <= 0 || rowCount <= 0) return -1;
  const int row = (p.y - bounds.y) / rowHeight;
  return row < rowCount ? row : -1;
}

int clippedRowAt(Point p, Rect bounds, int16_t rowHeight, int rowCount, int firstRow) {
  const int row = rowAt(p, bounds, rowHeight, rowCount - firstRow);
  return row < 0 ? -1 : firstRow + row;
}

int gridIndexAt(Point p, Rect bounds, int columns, int rows) {
  if (!bounds.contains(p) || columns <= 0 || rows <= 0) return -1;
  const int column = static_cast<int32_t>(p.x - bounds.x) * columns / bounds.width;
  const int row = static_cast<int32_t>(p.y - bounds.y) * rows / bounds.height;
  return row * columns + column;
}

int pagedRowAt(Point p, Rect bounds, int16_t rowHeight, int itemCount, int page, int rowsPerPage) {
  if (page < 0 || rowsPerPage <= 0) return -1;
  const int first = page * rowsPerPage;
  const int visible = itemCount - first < rowsPerPage ? itemCount - first : rowsPerPage;
  const int row = rowAt(p, bounds, rowHeight, visible);
  return row < 0 ? -1 : first + row;
}

Rect buttonBarButtonRect(int index, int16_t screenWidth, int16_t screenHeight, int16_t buttonWidth) {
  if (index < 0 || index >= 4 || screenWidth <= 0 || screenHeight <= 0 || buttonWidth <= 0) return {};
  const int totalGap = screenWidth > 4 * buttonWidth ? screenWidth - 4 * buttonWidth : 0;
  const int x = totalGap * (index + 1) / 5 + index * buttonWidth;
  return {static_cast<int16_t>(x), static_cast<int16_t>(screenHeight - 50), buttonWidth, 46};
}

int buttonBarIndex(Point p, int16_t screenWidth, int16_t screenHeight, int16_t buttonWidth) {
  for (int i = 0; i < 4; ++i) {
    if (buttonBarButtonRect(i, screenWidth, screenHeight, buttonWidth).contains(p)) return i;
  }
  return -1;
}

int semanticButtonBarIndex(Point p, int16_t screenWidth, int16_t screenHeight, bool frontLrbc) {
  const int visual = buttonBarIndex(p, screenWidth, screenHeight);
  if (!frontLrbc || visual < 0) return visual;
  constexpr int semanticByVisual[] = {2, 3, 0, 1};
  return semanticByVisual[visual];
}

DialogLayout confirmationDialogLayout(int16_t screenWidth, int16_t screenHeight, int16_t dialogHeight) {
  const int16_t dialogWidth = screenWidth - 60;
  const int16_t dialogY = (screenHeight - dialogHeight) / 2;
  const int16_t buttonY = dialogY + dialogHeight - 50;
  return {{30, dialogY, dialogWidth, dialogHeight},
          {{static_cast<int16_t>(30 + dialogWidth / 2 - 100), buttonY, 80, 30},
           {static_cast<int16_t>(30 + dialogWidth / 2 + 20), buttonY, 80, 30}}};
}

int dialogChoiceAt(Point p, const DialogLayout& layout) {
  for (int i = 0; i < 2; ++i) {
    if (layout.choices[i].contains(p)) return i;
  }
  return -1;
}
int popupMenuRowAt(Point p, Rect rows, int16_t rowHeight, int itemCount) {
  return rowAt(p, rows, rowHeight, itemCount);
}
int keyboardIndexAt(Point p, Rect grid, int columns, int rows) { return gridIndexAt(p, grid, columns, rows); }
}  // namespace ui::touch
