#include "MiniAppInput.h"

#include <GfxRenderer.h>

#include "../core/Core.h"
#include "../ui/Elements.h"

extern GfxRenderer renderer;

namespace papyrix {

bool miniAppButton(const Core& core, const Event& event, Button& button) {
  if (event.type == EventType::ButtonPress) {
    button = event.button;
    return true;
  }
  if (event.type != EventType::Tap) return false;
  const int index = ui::touch::semanticButtonBarIndex({event.touch.x, event.touch.y}, renderer.getScreenWidth(),
                                                      renderer.getScreenHeight(),
                                                      core.settings.frontButtonLayout == Settings::FrontLRBC);
  if (index < 0) return false;
  static constexpr Button buttons[] = {Button::Back, Button::Center, Button::Left, Button::Right};
  button = buttons[index];
  return true;
}

miniapps::Action handleAppButtons(Core& core, const Event& event, bool menuOpen, void (*onButton)(Core&, Button),
                                  void (*onMenuButton)(Core&, Button)) {
  Button button;
  if (!miniAppButton(core, event, button)) return miniapps::Action::None;
  if (button == Button::Back) return menuOpen ? miniapps::Action::CloseMenu : miniapps::Action::Exit;
  if (!menuOpen && button == Button::Center) return onMenuButton ? miniapps::Action::OpenMenu : miniapps::Action::None;
  auto handler = menuOpen ? onMenuButton : onButton;
  if (!handler) return miniapps::Action::None;
  handler(core, button);
  return miniapps::Action::Redraw;
}

}  // namespace papyrix
