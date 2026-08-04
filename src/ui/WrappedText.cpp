#include "WrappedText.h"

#include <algorithm>

namespace ui {

int centeredTextWrapped(const GfxRenderer& r, int fontId, int y, const char* text, int maxWidth, int maxLines,
                        bool black, EpdFontFamily::Style style) {
  if (!text || text[0] == '\0' || maxWidth <= 0 || maxLines <= 0) return 0;

  const auto lines = r.wrapTextWithHyphenation(fontId, text, maxWidth, maxLines, style);
  const int count = std::min(static_cast<int>(lines.size()), maxLines);
  const int lineHeight = r.getLineHeight(fontId);
  for (int i = 0; i < count; ++i) {
    r.drawCenteredText(fontId, y + i * lineHeight, lines[i].c_str(), black, style);
  }
  return count;
}

int centeredText(const GfxRenderer& r, const Theme& t, int y, const char* text) {
  const int maxWidth = r.getScreenWidth() - 2 * (t.screenMarginSide + t.itemPaddingX);
  return centeredTextWrapped(r, t.uiFontId, y, text, maxWidth, 2, t.primaryTextBlack);
}

}  // namespace ui
