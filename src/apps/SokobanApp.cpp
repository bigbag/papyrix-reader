#include "SokobanApp.h"

#include <GfxRenderer.h>
#include <HardwareIdentity.h>
#include <I18n.h>
#include <ThemeManager.h>

#include <algorithm>

#include "../core/Core.h"
#include "../ui/Elements.h"
#include "../ui/views/SokobanView.h"
#include "MiniAppInput.h"
#include "SokobanStore.h"

extern GfxRenderer renderer;

namespace papyrix::sokoban_app {
namespace {
using miniapps::Action;
enum class Prompt : uint8_t { None, Picker, Confirm, LoadError, ClearError, ExitError };
struct State {
  sokoban::Game game;
  Prompt prompt = Prompt::None;
  Prompt previousPrompt = Prompt::None;
  Prompt targetReturn = Prompt::None;
  Prompt returnPrompt = Prompt::None;
  int selected = 0;
  int menuSelected = 0;
  int page = 0;
  uint8_t target = 0;
  bool loaded = false;
  bool dirty = false;
  bool confirmPending = false;
} state;
constexpr int kRowsTop = 60;
constexpr int kPageSize = 4;

bool hasTouch() { return board::hasTouch(board::HardwareIdentity::instance().profile()); }
int rowHeight() {
  const int available = (renderer.getScreenHeight() - kRowsTop - 50) / (kPageSize + 2);
  return std::min(std::max(int(THEME.itemHeight), hasTouch() ? 46 : 30), available);
}

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

void picker(Core& core, Prompt returnPrompt) {
  state.returnPrompt = returnPrompt;
  state.page = state.loaded ? state.game.level / kPageSize : 0;
  prompt(core, Prompt::Picker);
}

Action startLevel(Core& core) {
  if (!sokoban::clear(core.storage)) {
    prompt(core, Prompt::ClearError);
    return Action::Redraw;
  }
  sokoban::start(state.game, state.target);
  state.loaded = true;
  state.dirty = true;
  state.menuSelected = 0;
  prompt(core, Prompt::None);
  return Action::CloseMenu;
}

Action nextLevel(Core& core) {
  if (state.game.level + 1 >= sokoban::kLevelCount) {
    picker(core, Prompt::None);
    return Action::Redraw;
  }
  state.target = state.game.level + 1;
  state.targetReturn = Prompt::None;
  return startLevel(core);
}

int options(const char** labels, const char*& title) {
  title = tr(SOKOBAN);
  if (state.prompt == Prompt::Picker) {
    static const char* levels[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"};
    title = tr(SOKOBAN_LEVELS);
    for (int i = 0; i < kPageSize; ++i) labels[i] = levels[state.page * kPageSize + i];
    labels[4] = "<";
    labels[5] = ">";
    return 6;
  }
  if (state.prompt == Prompt::LoadError) {
    title = tr(GAME_LOAD_ERROR);
    labels[0] = tr(RETRY);
    labels[1] = tr(SOKOBAN_LEVELS);
    labels[2] = tr(BACK);
    return 3;
  }
  if (state.prompt == Prompt::ClearError) {
    title = tr(GAME_CLEAR_ERROR);
    labels[0] = tr(RETRY);
    labels[1] = tr(BACK);
    return 2;
  }
  if (state.prompt == Prompt::ExitError) {
    title = tr(SAVE_FAILED);
    labels[0] = tr(RETRY);
    labels[1] = tr(CANCEL);
    labels[2] = tr(GAME_DISCARD);
    return 3;
  }
  labels[0] = tr(GAME_RESUME);
  labels[1] = tr(UNDO);
  labels[2] = tr(SOKOBAN_RESTART);
  labels[3] = tr(SOKOBAN_LEVELS);
  labels[4] = tr(SAVE_EXIT);
  return 5;
}

Action choose(Core& core, int choice) {
  switch (state.prompt) {
    case Prompt::Confirm:
      if (choice == 0) return startLevel(core);
      prompt(core, state.targetReturn);
      return Action::Redraw;
    case Prompt::ClearError:
      if (choice == 0) return startLevel(core);
      prompt(core, state.targetReturn);
      return Action::Redraw;
    case Prompt::LoadError:
      if (choice == 0) {
        enter(core);
        return Action::Redraw;
      }
      if (choice == 2) return Action::Exit;
      picker(core, Prompt::LoadError);
      return Action::Redraw;
    case Prompt::ExitError:
      return choice == 0 ? Action::RetryExit : choice == 1 ? Action::CancelExit : Action::DiscardExit;
    case Prompt::Picker:
      if (choice >= kPageSize) {
        const int pages = (sokoban::kLevelCount + kPageSize - 1) / kPageSize;
        state.page = (state.page + (choice == kPageSize ? pages - 1 : 1)) % pages;
        state.selected = 0;
        return Action::Redraw;
      }
      state.target = state.page * kPageSize + choice;
      state.targetReturn = Prompt::Picker;
      if (state.loaded && sokoban::isWon(state.game)) return startLevel(core);
      prompt(core, Prompt::Confirm, 1);
      return Action::Redraw;
    case Prompt::None:
      if (choice == 0) return Action::CloseMenu;
      if (choice == 1) {
        if (sokoban::undo(state.game)) state.dirty = true;
        return Action::CloseMenu;
      }
      if (choice == 4) return Action::Exit;
      if (choice == 3) {
        picker(core, Prompt::None);
        return Action::Redraw;
      }
      state.target = state.game.level;
      state.targetReturn = Prompt::None;
      prompt(core, Prompt::Confirm, 1);
      return Action::Redraw;
  }
  return Action::None;
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
    state.menuSelected = 0;
    return Action::OpenMenu;
  }
  if (event.type == EventType::ButtonRelease && event.button == Button::Center) {
    if (!state.confirmPending) return Action::None;
    state.confirmPending = false;
    if (modal || menuOpen || !state.loaded) return Action::None;
    button = Button::Center;
  } else if (event.type == EventType::ButtonPress) {
    button = event.button;
    if (button == Button::Center && !modal && !menuOpen && state.loaded) {
      state.confirmPending = true;
      return Action::None;
    }
  } else if (event.type == EventType::Tap && hasTouch()) {
    state.confirmPending = false;
    const ui::touch::Point point{event.touch.x, event.touch.y};
    int choice = -1;
    if (state.prompt == Prompt::Confirm) {
      choice = ui::touch::dialogChoiceAt(point, ui::confirmationDialogBounds(renderer, THEME, tr(GAME_RESTART)));
    } else if (modal || menuOpen) {
      const char* labels[6];
      const char* title;
      const int count = options(labels, title);
      choice = ui::touch::rowAt(point, {0, kRowsTop, int16_t(renderer.getScreenWidth()), int16_t(count * rowHeight())},
                                rowHeight(), count);
    } else if (state.loaded) {
      const auto layout = ui::sokobanLayout(renderer.getScreenWidth(), renderer.getScreenHeight(), state.game, true);
      if (layout.menu.contains(point)) {
        state.menuSelected = 0;
        return Action::OpenMenu;
      }
      if (layout.back.contains(point))
        button = Button::Back;
      else if (layout.undo.contains(point))
        button = Button::Center;
      else {
        for (int i = 0; i < 4; ++i) {
          if (layout.directions[i].contains(point)) {
            if (!sokoban::isWon(state.game) && sokoban::move(state.game, static_cast<sokoban::Direction>(i))) {
              state.dirty = true;
              return Action::Redraw;
            }
            return Action::None;
          }
        }
        return Action::None;
      }
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
    if (state.prompt == Prompt::Confirm || state.prompt == Prompt::ClearError) return choose(core, 1);
    if (state.prompt == Prompt::Picker) {
      prompt(core, state.returnPrompt);
      return Action::Redraw;
    }
    if (state.prompt == Prompt::LoadError) return Action::Exit;
    return menuOpen ? Action::CloseMenu : Action::Exit;
  }
  if (modal || menuOpen) {
    const char* labels[6];
    const char* title;
    const int count = state.prompt == Prompt::Confirm ? 2 : options(labels, title);
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
    if (sokoban::isWon(state.game)) return nextLevel(core);
    if (!sokoban::undo(state.game)) return Action::None;
  } else {
    if (sokoban::isWon(state.game)) return Action::None;
    sokoban::Direction direction;
    if (button == Button::Up)
      direction = sokoban::Direction::Up;
    else if (button == Button::Down)
      direction = sokoban::Direction::Down;
    else if (button == Button::Left)
      direction = sokoban::Direction::Left;
    else if (button == Button::Right)
      direction = sokoban::Direction::Right;
    else
      return Action::None;
    if (!sokoban::move(state.game, direction)) return Action::None;
  }
  state.dirty = true;
  return Action::Redraw;
}
}  // namespace

void enter(Core& core) {
  state = {};
  cancelInput(core);
  const auto result = sokoban::load(core.storage, state.game);
  if (result == sokoban::LoadResult::Missing) {
    sokoban::start(state.game, 0);
    state.loaded = true;
    state.dirty = true;
  } else if (result == sokoban::LoadResult::Loaded) {
    state.loaded = true;
  } else {
    prompt(core, Prompt::LoadError);
  }
}

bool update(Core& core) {
  core.cpu.throttle();
  return false;
}

miniapps::Frame render(Core&, bool menuOpen) {
  if (state.prompt == Prompt::Confirm && state.loaded) {
    ui::renderSokoban(renderer, THEME, state.game, hasTouch());
  } else if (menuOpen || state.prompt != Prompt::None) {
    const char* labels[6];
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
    ui::renderSokoban(renderer, THEME, state.game, hasTouch());
  }
  if (state.prompt == Prompt::Confirm) ui::dialog(renderer, THEME, tr(SOKOBAN), tr(GAME_RESTART), state.selected);
  return {miniapps::Refresh::Fast, true};
}

miniapps::ExitResult prepareExit(Core& core, miniapps::ExitTarget target) {
  if (target == miniapps::ExitTarget::Menu) return miniapps::ExitResult::Ready;
  if (!state.loaded || !state.dirty || sokoban::save(core.storage, state.game)) {
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
    state.selected = state.prompt == Prompt::Confirm ? 1 : 0;
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
}  // namespace papyrix::sokoban_app
