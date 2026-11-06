#include "Game2048App.h"

#include <GfxRenderer.h>
#include <HardwareIdentity.h>
#include <I18n.h>
#include <ThemeManager.h>
#include <esp_system.h>

#include <algorithm>

#include "../core/Core.h"
#include "../ui/Elements.h"
#include "../ui/views/Game2048View.h"
#include "Game2048Store.h"

extern GfxRenderer renderer;

namespace papyrix::game2048_app {
using miniapps::Action;
static void cancelGameInput(Core& core);
namespace {
enum class Prompt : uint8_t { None, Restart, Win, Over, LoadError, RestartError, ClearError, ExitError };
struct State {
  game2048::Game game;
  Prompt prompt = Prompt::None;
  Prompt previousPrompt = Prompt::None;
  int selected = 0;
  int menuSelected = 0;
  bool hasGame = false;
  bool dirty = false;
  bool clearPending = false;
  bool fullRefresh = true;
} state;

bool hasTouch() { return board::hasTouch(board::HardwareIdentity::instance().profile()); }
int rowHeight() { return std::max(int(THEME.itemHeight), hasTouch() ? 46 : 30); }
constexpr int kRowsTop = 60;

void prompt(Core& core, Prompt value, int selected = 0) {
  state.prompt = value;
  state.selected = selected;
  cancelGameInput(core);
}

void spawn() { game2048::spawn(state.game, esp_random(), esp_random()); }

void newGame(Core& core) {
  state.game = {};
  spawn();
  spawn();
  state.hasGame = true;
  state.dirty = true;
  state.clearPending = false;
  state.menuSelected = 0;
  prompt(core, Prompt::None);
}

Action restart(Core& core) {
  if (!game2048::clear(core.storage)) {
    prompt(core, Prompt::RestartError);
    return Action::Redraw;
  }
  newGame(core);
  return Action::CloseMenu;
}

int options(const char** labels, const char*& title) {
  title = "2048";
  switch (state.prompt) {
    case Prompt::Win:
      title = "2048!";
      labels[0] = tr(GAME_CONTINUE);
      labels[1] = tr(GAME_NEW);
      return 2;
    case Prompt::Over:
      title = tr(GAME_OVER);
      labels[0] = tr(GAME_NEW);
      labels[1] = tr(BACK);
      return 2;
    case Prompt::LoadError:
      title = tr(GAME_LOAD_ERROR);
      labels[0] = tr(RETRY);
      labels[1] = tr(GAME_NEW);
      labels[2] = tr(BACK);
      return 3;
    case Prompt::RestartError:
    case Prompt::ClearError:
      title = tr(GAME_CLEAR_ERROR);
      labels[0] = tr(RETRY);
      labels[1] = tr(BACK);
      return 2;
    case Prompt::ExitError:
      title = state.clearPending ? tr(GAME_CLEAR_ERROR) : tr(SAVE_FAILED);
      labels[0] = tr(RETRY);
      labels[1] = tr(CANCEL);
      labels[2] = tr(GAME_DISCARD);
      return 3;
    default:
      labels[0] = tr(GAME_RESUME);
      labels[1] = tr(GAME_NEW);
      return 2;
  }
}

void drawOptions() {
  const char* labels[3];
  const char* heading;
  const int count = options(labels, heading);
  renderer.clearScreen(THEME.backgroundColor);
  ui::title(renderer, THEME, THEME.screenMarginTop, heading);
  const int selected = state.prompt == Prompt::None ? state.menuSelected : state.selected;
  for (int i = 0; i < count; ++i)
    ui::enumValue(renderer, THEME, kRowsTop + i * rowHeight(), labels[i], "", i == selected, rowHeight());
  if (state.prompt == Prompt::ExitError)
    renderer.drawCenteredText(THEME.smallFontId, kRowsTop + count * rowHeight() + 24, tr(GAME_KEEP_SAVE),
                              THEME.primaryTextBlack);
  ui::buttonBar(renderer, THEME, ui::ButtonBar(tr(BACK), tr(OPEN)));
}

Action choose(Core& core, int selected) {
  switch (state.prompt) {
    case Prompt::Restart:
      if (selected == 0) return restart(core);
      prompt(core, state.hasGame ? Prompt::None : Prompt::LoadError);
      return Action::Redraw;
    case Prompt::Win:
      if (selected == 0) {
        state.game.wonAcknowledged = true;
        state.dirty = true;
        prompt(core, Prompt::None);
        return Action::Redraw;
      }
      break;
    case Prompt::Over:
      if (selected == 1) return Action::Exit;
      break;
    case Prompt::LoadError:
      if (selected == 0) {
        enter(core);
        return Action::Redraw;
      }
      if (selected == 2) return Action::Exit;
      break;
    case Prompt::RestartError:
      if (selected == 0) return restart(core);
      prompt(core, state.hasGame ? Prompt::None : Prompt::LoadError);
      return Action::Redraw;
    case Prompt::ClearError:
      if (selected == 0 && !game2048::clear(core.storage)) return Action::Redraw;
      if (selected == 0) state.clearPending = false;
      prompt(core, Prompt::Over);
      return Action::Redraw;
    case Prompt::ExitError:
      return selected == 0 ? Action::RetryExit : selected == 1 ? Action::CancelExit : Action::DiscardExit;
    case Prompt::None:
      if (selected == 0) return Action::CloseMenu;
      break;
  }
  prompt(core, Prompt::Restart, 1);
  return Action::Redraw;
}

Action back(Core& core, bool menuOpen) {
  switch (state.prompt) {
    case Prompt::ExitError:
      return Action::CancelExit;
    case Prompt::Restart:
      return choose(core, 1);
    case Prompt::Win:
      return choose(core, 0);
    case Prompt::RestartError:
    case Prompt::ClearError:
      return choose(core, 1);
    case Prompt::Over:
    case Prompt::LoadError:
      return Action::Exit;
    case Prompt::None:
      return menuOpen ? Action::CloseMenu : Action::Exit;
  }
  return Action::None;
}
}  // namespace

static void cancelGameInput(Core& core) {
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

void enter(Core& core) {
  state = {};
  cancelGameInput(core);
  const auto result = game2048::load(core.storage, state.game);
  if (result == game2048::LoadResult::Missing)
    newGame(core);
  else if (result == game2048::LoadResult::Loaded) {
    state.hasGame = true;
    if (game2048::isWon(state.game) && !state.game.wonAcknowledged) prompt(core, Prompt::Win);
  } else
    prompt(core, Prompt::LoadError);
}

bool update(Core& core) {
  // Keep touch polling fast enough to capture short swipes.
  if (hasTouch() && state.prompt == Prompt::None)
    core.cpu.unthrottle();
  else
    core.cpu.throttle();
  return false;
}

miniapps::Frame render(Core&, bool menuOpen) {
  if (menuOpen) {
    drawOptions();
    if (state.prompt == Prompt::Restart) ui::dialog(renderer, THEME, "2048", tr(GAME_RESTART), state.selected);
  } else if (state.prompt != Prompt::None && state.prompt != Prompt::Restart) {
    drawOptions();
  } else {
    ui::renderGame2048(renderer, THEME, state.game, hasTouch());
    if (state.prompt == Prompt::Restart) ui::dialog(renderer, THEME, "2048", tr(GAME_RESTART), state.selected);
  }
  const auto refresh = state.fullRefresh ? miniapps::Refresh::Full : miniapps::Refresh::Fast;
  state.fullRefresh = false;
  return {refresh, true};
}

void exit(Core& core) {
  cancelGameInput(core);
  state = {};
  core.cpu.unthrottle();
}

miniapps::ExitResult prepareExit(Core& core, miniapps::ExitTarget target) {
  if (target == miniapps::ExitTarget::Menu) return miniapps::ExitResult::Ready;
  const bool saved = state.clearPending ? game2048::clear(core.storage)
                                        : !state.hasGame || !state.dirty || game2048::save(core.storage, state.game);
  if (saved) {
    state.dirty = false;
    state.clearPending = false;
    return miniapps::ExitResult::Ready;
  }
  if (state.prompt != Prompt::ExitError) state.previousPrompt = state.prompt;
  prompt(core, Prompt::ExitError, 1);
  return miniapps::ExitResult::Blocked;
}

static Action handleGameEvent(Core& core, const Event& event, bool menuOpen) {
  const bool modal = state.prompt != Prompt::None;
  Button button = Button::Count;
  int choice = -1;
  if (event.type == EventType::ButtonPress)
    button = event.button;
  else if (event.type == EventType::Tap) {
    const ui::touch::Point point{event.touch.x, event.touch.y};
    if (state.prompt == Prompt::Restart) {
      choice = ui::touch::dialogChoiceAt(point, ui::confirmationDialogBounds(renderer, THEME, tr(GAME_RESTART)));
    } else if (modal || menuOpen) {
      const char* labels[3];
      const char* title;
      const int count = options(labels, title);
      choice = ui::touch::rowAt(point, {0, kRowsTop, int16_t(renderer.getScreenWidth()), int16_t(count * rowHeight())},
                                rowHeight(), count);
    } else if (hasTouch()) {
      const auto layout = ui::game2048Layout(renderer.getScreenWidth(), renderer.getScreenHeight(), true);
      const int arrow = ui::game2048ArrowAt(layout, point);
      static constexpr Button directions[] = {Button::Left, Button::Up, Button::Down, Button::Right};
      if (arrow >= 0) button = directions[arrow];
    }
    if (choice >= 0) return choose(core, choice);
    if (button == Button::Count) {
      const int footer = ui::touch::semanticButtonBarIndex(point, renderer.getScreenWidth(), renderer.getScreenHeight(),
                                                           core.settings.frontButtonLayout == Settings::FrontLRBC);
      if (footer == 0) button = Button::Back;
      if (footer == 1) button = Button::Center;
    }
  } else if (event.type == EventType::Swipe && !modal && !menuOpen) {
    const auto layout = ui::game2048Layout(renderer.getScreenWidth(), renderer.getScreenHeight(), hasTouch());
    if (layout.board.contains({event.gesture.start.x, event.gesture.start.y})) button = event.gesture.direction;
  }
  if (button == Button::Count || button == Button::Power) return Action::None;
  if (button == Button::Back) return back(core, menuOpen);
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
  if (button == Button::Center) return Action::OpenMenu;
  game2048::Direction direction;
  switch (button) {
    case Button::Left:
      direction = game2048::Direction::Left;
      break;
    case Button::Up:
      direction = game2048::Direction::Up;
      break;
    case Button::Down:
      direction = game2048::Direction::Down;
      break;
    case Button::Right:
      direction = game2048::Direction::Right;
      break;
    default:
      return Action::None;
  }
  if (!state.hasGame || !game2048::slide(state.game, direction)) return Action::None;
  spawn();
  state.dirty = true;
  if (game2048::isStuck(state.game)) {
    state.clearPending = !game2048::clear(core.storage);
    state.dirty = false;
    prompt(core, state.clearPending ? Prompt::ClearError : Prompt::Over);
  } else if (game2048::isWon(state.game) && !state.game.wonAcknowledged)
    prompt(core, Prompt::Win);
  return Action::Redraw;
}

miniapps::Action handleEvent(Core& core, const Event& event, bool menuOpen) {
  const auto action = handleGameEvent(core, event, menuOpen);
  if (action == Action::OpenMenu || action == Action::CloseMenu || action == Action::CancelExit) {
    if (action == Action::CancelExit) {
      state.prompt = state.previousPrompt;
      state.selected = state.prompt == Prompt::Restart ? 1 : 0;
      core.input.resetIdleTimer();
    }
    cancelGameInput(core);
  }
  return action;
}
}  // namespace papyrix::game2048_app
