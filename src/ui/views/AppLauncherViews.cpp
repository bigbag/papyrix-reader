#include "AppLauncherViews.h"

#include <I18n.h>

#include "../../apps/MiniApp.h"

namespace ui {

void render(const GfxRenderer& r, const Theme& t, const AppMenuView& v, const papyrix::MiniApp* apps) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(APPS));

  const int startY = 60;
  for (int i = 0; i < v.itemCount; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    const char* name = (i < AppMenuView::EXTRA_COUNT) ? (i == 0 ? tr(WIFI_TRANSFER) : tr(CALIBRE_SYNC))
                                                      : apps[i - AppMenuView::EXTRA_COUNT].name;
    menuItem(r, t, y, name, i == v.selected);
  }

  ButtonBar btns{tr(BACK), tr(OPEN), "", ""};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

}  // namespace ui
