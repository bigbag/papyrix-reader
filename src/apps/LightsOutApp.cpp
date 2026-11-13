#include "LightsOutApp.h"

#include <GfxRenderer.h>
#include <HardwareIdentity.h>
#include <I18n.h>
#include <ThemeManager.h>
#include <esp_system.h>

#include <algorithm>

#include "../core/Core.h"
#include "../ui/Elements.h"
#include "../ui/views/LightsOutView.h"
#include "LightsOutStore.h"
#include "MiniAppInput.h"

extern GfxRenderer renderer;

namespace papyrix::lightsout_app {
namespace {
using miniapps::Action;
enum class Prompt : uint8_t { None, Restart, LoadError, RestartError, ExitError };
struct State {
  lightsout::Game game;
  Prompt prompt = Prompt::None;
  Prompt previousPrompt = Prompt::None;
  int selected = 0;
  int menuSelected = 0;
  bool loaded = false;
  bool dirty = false;
  bool confirmPending = false;
} state;
constexpr int kRowsTop = 60;

bool hasTouch() { return board::hasTouch(board::HardwareIdentity::instance().profile()); }
int rowHeight() { return std::max(int(THEME.itemHeight), hasTouch() ? 46 : 30); }

void cancelInput(Core& core) {
  state.confirmPending = false;
  core.input.cancelTouchGesture();
  const size_t count = core.events.size();
  for (size_t i = 0; i < count; ++i) {
    Event event;
    if (!core.events.pop(event)) break;
    const bool button = event.type >= EventType::ButtonPress && event.type <= EventType::ButtonCancel;
    if (event.type == EventType::Tap || event.type == EventType::Swipe || (button && event.button != Button::Power))
      continue;
    core.events.push(event);
  }
}

void prompt(Core& core, Prompt value, int selected = 0) {
  state.prompt = value;
  state.selected = selected;
  cancelInput(core);
}

void newGame(Core& core) {
  state.game = lightsout::generate(esp_random());
  state.loaded = true;
  state.dirty = true;
  state.menuSelected = 0;
  prompt(core, Prompt::None);
}

Action restart(Core& core) {
  if (!lightsout::clear(core.storage)) {
    prompt(core, Prompt::RestartError);
    return Action::Redraw;
  }
  newGame(core);
  return Action::CloseMenu;
}

int options(const char** labels, const char*& title) {
  title = tr(LIGHTS_OUT);
  if (state.prompt == Prompt::LoadError) {
    title = tr(GAME_LOAD_ERROR);
    labels[0] = tr(RETRY);
    labels[1] = tr(GAME_NEW);
    labels[2] = tr(BACK);
  } else if (state.prompt == Prompt::RestartError) {
    title = tr(GAME_CLEAR_ERROR);
    labels[0] = tr(RETRY);
    labels[1] = tr(BACK);
    return 2;
  } else if (state.prompt == Prompt::ExitError) {
    title = tr(SAVE_FAILED);
    labels[0] = tr(RETRY);
    labels[1] = tr(CANCEL);
    labels[2] = tr(GAME_DISCARD);
  } else {
    labels[0] = tr(GAME_RESUME);
    labels[1] = tr(GAME_NEW);
    labels[2] = tr(SAVE_EXIT);
  }
  return 3;
}

Action choose(Core& core, int choice) {
  switch (state.prompt) {
    case Prompt::Restart:
      if (choice == 0) return restart(core);
      prompt(core, state.loaded ? Prompt::None : Prompt::LoadError);
      return Action::Redraw;
    case Prompt::RestartError:
      if (choice == 0) return restart(core);
      prompt(core, state.loaded ? Prompt::None : Prompt::LoadError);
      return Action::Redraw;
    case Prompt::LoadError:
      if (choice == 0) {
        enter(core);
        return Action::Redraw;
      }
      if (choice == 2) return Action::Exit;
      break;
    case Prompt::ExitError:
      return choice == 0 ? Action::RetryExit : choice == 1 ? Action::CancelExit : Action::DiscardExit;
    case Prompt::None:
      if (choice == 0) return Action::CloseMenu;
      if (choice == 2) return Action::Exit;
      break;
  }
  prompt(core, Prompt::Restart, 1);
  return Action::Redraw;
}
Action openMenu() {
  state.menuSelected = 0;
  return Action::OpenMenu;
}

Action handleInput(Core& core, const Event& event, bool menuOpen) {
  const bool modal = state.prompt != Prompt::None;
  Button button = Button::Count;
  if (event.type == EventType::ButtonCancel && event.button == Button::Center) {
    state.confirmPending = false;
    return Action::None;
  }
  if (event.type == EventType::ButtonLongPress && event.button == Button::Center) {
    if (!state.confirmPending) return Action::None;
    state.confirmPending = false;
    return openMenu();
  }
  if (event.type == EventType::ButtonRelease && event.button == Button::Center) {
    if (!state.confirmPending) return Action::None;
    state.confirmPending = false;
    if (modal || menuOpen || !state.loaded) return Action::None;
    button = Button::Center;
  } else if (event.type == EventType::ButtonPress || event.type == EventType::ButtonRepeat) {
    button = event.button;
    if (event.type == EventType::ButtonRepeat &&
        (modal || menuOpen || button == Button::Center || button == Button::Back))
      return Action::None;
    if (button == Button::Center && !modal && !menuOpen && state.loaded) {
      state.confirmPending = true;
      return Action::None;
    }
  } else if (event.type == EventType::Tap && hasTouch()) {
    state.confirmPending = false;
    const ui::touch::Point point{event.touch.x, event.touch.y};
    int choice = -1;
    if (state.prompt == Prompt::Restart) {
      choice = ui::touch::dialogChoiceAt(point, ui::confirmationDialogBounds(renderer, THEME, tr(GAME_RESTART)));
    } else if (modal || menuOpen) {
      const char* labels[3];
      const char* title;
      const int count = options(labels, title);
      choice = ui::touch::rowAt(point, {0, kRowsTop, int16_t(renderer.getScreenWidth()), int16_t(count * rowHeight())},
                                rowHeight(), count);
    } else if (state.loaded) {
      const auto layout = ui::lightsOutLayout(renderer.getScreenWidth(), renderer.getScreenHeight(), true);
      const int cell = ui::lightsOutCellAt(layout, point);
      if (cell >= 0 && !lightsout::isWon(state.game)) {
        state.game.cursor = static_cast<uint8_t>(cell);
        state.dirty = true;
        return Action::Redraw;
      }
      if (layout.menu.contains(point)) return openMenu();
      if (layout.back.contains(point))
        button = Button::Back;
      else if (layout.toggle.contains(point))
        button = Button::Center;
      else
        return Action::None;
    }
    if (choice >= 0) return choose(core, choice);
    if (button == Button::Count &&
        (!miniAppButton(core, event, button) || (button != Button::Back && button != Button::Center)))
      return Action::None;
  } else {
    return Action::None;
  }
  if (button == Button::Power || button == Button::Count) return Action::None;
  if (button == Button::Back) {
    if (state.prompt == Prompt::ExitError) return Action::CancelExit;
    if (state.prompt == Prompt::Restart || state.prompt == Prompt::RestartError) return choose(core, 1);
    if (state.prompt == Prompt::LoadError) return Action::Exit;
    return menuOpen ? Action::CloseMenu : Action::Exit;
  }
  if (modal || menuOpen) {
    const char* labels[3];
    const char* title;
    const int count = state.prompt == Prompt::Restart ? 2 : options(labels, title);
    int& selected = modal ? state.selected : state.menuSelected;
    if (button == Button::Center) return choose(core, selected);
    if (button == Button::Up || button == Button::Left)
      selected = (selected + count - 1) % count;
    else if (button == Button::Down || button == Button::Right)
      selected = (selected + 1) % count;
    else
      return Action::None;
    return Action::Redraw;
  }
  if (!state.loaded) return Action::None;
  if (button == Button::Center) {
    if (lightsout::isWon(state.game)) {
      prompt(core, Prompt::Restart, 1);
      return Action::Redraw;
    }
    if (!lightsout::toggle(state.game, state.game.cursor)) return Action::None;
  } else {
    if (lightsout::isWon(state.game)) return Action::None;
    int x = state.game.cursor % lightsout::kSide;
    int y = state.game.cursor / lightsout::kSide;
    if (button == Button::Left)
      x = std::max(0, x - 1);
    else if (button == Button::Right)
      x = std::min(int(lightsout::kSide) - 1, x + 1);
    else if (button == Button::Up)
      y = std::max(0, y - 1);
    else if (button == Button::Down)
      y = std::min(int(lightsout::kSide) - 1, y + 1);
    else
      return Action::None;
    state.game.cursor = static_cast<uint8_t>(y * lightsout::kSide + x);
  }
  state.dirty = true;
  return Action::Redraw;
}
}  // namespace

void enter(Core& core) {
  state = {};
  cancelInput(core);
  const auto result = lightsout::load(core.storage, state.game);
  if (result == lightsout::LoadResult::Missing)
    newGame(core);
  else if (result == lightsout::LoadResult::Loaded)
    state.loaded = true;
  else
    prompt(core, Prompt::LoadError);
}

bool update(Core& core) {
  core.cpu.throttle();
  return false;
}

miniapps::Frame render(Core&, bool menuOpen) {
  if (menuOpen || (state.prompt != Prompt::None && state.prompt != Prompt::Restart)) {
    const char* labels[3];
    const char* title;
    const int count = options(labels, title);
    renderer.clearScreen(THEME.backgroundColor);
    ui::title(renderer, THEME, THEME.screenMarginTop, title);
    const int selected = state.prompt == Prompt::None ? state.menuSelected : state.selected;
    for (int i = 0; i < count; ++i)
      ui::enumValue(renderer, THEME, kRowsTop + i * rowHeight(), labels[i], "", i == selected, rowHeight());
    if (state.prompt == Prompt::ExitError)
      renderer.drawCenteredText(THEME.smallFontId, kRowsTop + count * rowHeight() + 24, tr(GAME_KEEP_SAVE),
                                THEME.primaryTextBlack);
    ui::buttonBar(renderer, THEME, ui::ButtonBar(tr(BACK), tr(OPEN)));
  } else {
    ui::renderLightsOut(renderer, THEME, state.game, hasTouch());
  }
  if (state.prompt == Prompt::Restart) ui::dialog(renderer, THEME, tr(LIGHTS_OUT), tr(GAME_RESTART), state.selected);
  return {miniapps::Refresh::Fast, true};
}

miniapps::ExitResult prepareExit(Core& core, miniapps::ExitTarget target) {
  if (target == miniapps::ExitTarget::Menu) return miniapps::ExitResult::Ready;
  if (!state.loaded || !state.dirty || lightsout::save(core.storage, state.game)) {
    state.dirty = false;
    return miniapps::ExitResult::Ready;
  }
  if (state.prompt != Prompt::ExitError) state.previousPrompt = state.prompt;
  prompt(core, Prompt::ExitError, 1);
  return miniapps::ExitResult::Blocked;
}

miniapps::Action handleEvent(Core& core, const Event& event, bool menuOpen) {
  const auto action = handleInput(core, event, menuOpen);
  if (action == Action::CancelExit) {
    state.prompt = state.previousPrompt;
    state.selected = state.prompt == Prompt::Restart ? 1 : 0;
    core.input.resetIdleTimer();
  }
  if (action == Action::OpenMenu || action == Action::CloseMenu || action == Action::CancelExit) cancelInput(core);
  return action;
}

void exit(Core& core) {
  cancelInput(core);
  state = {};
  core.cpu.unthrottle();
}
}  // namespace papyrix::lightsout_app
