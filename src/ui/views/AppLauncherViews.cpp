#include "AppLauncherViews.h"

#include <I18n.h>

#include <algorithm>

#include "../../apps/MiniApp.h"

namespace ui {

void render(const GfxRenderer& r, const Theme& t, const AppMenuView& v, const papyrix::MiniApp* apps) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(APPS));

  const int rowHeight = v.rowPitch(r.getScreenHeight(), t.itemHeight + t.itemSpacing);
  const int itemHeight = std::min<int>(t.itemHeight, rowHeight);
  const int left = t.screenMarginSide;
  const int width = r.getScreenWidth() - 2 * left;
  for (int i = 0; i < v.itemCount && rowHeight > 0; i++) {
    const int y = AppMenuView::rowY(i, rowHeight);
    if (i == 4 || i == 7) {
      const int separatorY = y - AppMenuView::GROUP_GAP / 2;
      const int end = r.getScreenWidth() - t.screenMarginSide;
      for (int x = t.screenMarginSide; x < end; x += 7) {
        r.fillRect(x, separatorY, std::min(3, end - x), 1, t.primaryTextBlack);
      }
    }
    const char* name = (i < AppMenuView::EXTRA_COUNT) ? (i == 0 ? tr(WIFI_TRANSFER) : tr(CALIBRE_SYNC))
                                                      : apps[appIdForMenuItem(i)].name;
    const int textY = y + (itemHeight - r.getLineHeight(t.uiFontId)) / 2;
    if (i == v.selected) r.fillRect(left, y, width, itemHeight, t.selectionFillBlack);
    r.drawText(t.uiFontId, left + t.itemPaddingX, textY, name,
               i == v.selected ? t.selectionTextBlack : t.primaryTextBlack);
  }

  ButtonBar btns{tr(BACK), tr(OPEN), "", ""};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

}  // namespace ui
