#include "AppLauncherViews.h"

#include "../../apps/MiniApp.h"

namespace ui {

constexpr const char* const AppMenuView::EXTRA_ITEMS[];

void render(const GfxRenderer& r, const Theme& t, const AppMenuView& v, const papyrix::MiniApp* apps) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Apps");

  const int startY = contentStartY(r, t);
  for (int i = 0; i < v.itemCount; i++) {
    const int y = startY + i * (t.itemHeight + t.itemSpacing);
    const char* name =
        (i < AppMenuView::EXTRA_COUNT) ? AppMenuView::EXTRA_ITEMS[i] : apps[i - AppMenuView::EXTRA_COUNT].name;
    menuItem(r, t, y, name, i == v.selected);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

}  // namespace ui
