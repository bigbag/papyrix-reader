#include "GoApp.h"

#include <BackgroundTask.h>
#include <GfxRenderer.h>
#include <GoSearch.h>
#include <HardwareIdentity.h>
#include <I18n.h>
#include <Logging.h>
#include <ThemeManager.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <algorithm>
#include <memory>
#include <new>

#include "../core/Core.h"
#include "../ui/Elements.h"
#include "../ui/views/GoView.h"
#include "GoStore.h"
#include "MiniAppInput.h"

extern GfxRenderer renderer;

namespace papyrix::go_app {
namespace {
using miniapps::Action;
constexpr char kTag[] = "GO";
constexpr uint32_t kStackBytes = 8192;
constexpr uint32_t kStopWaitMs = 250;
constexpr uint32_t kSearchBudgetMs = 5000;
constexpr int kRowsTop = 60;
enum class Prompt : uint8_t { None, Load, Memory, Write, Think, Restart, Resign, StopMenu, StopExit };
enum class Operation : uint8_t { None, Move, New, Resign };
enum class Command : uint8_t { Resume, Pass, Resign, New, Exit };

struct Runtime {
  go_store::Game game;
  gogame::Search search;
  go_store::HistoryReader reader;
  BackgroundTask task;
  gogame::SearchResult result;
  uint32_t searchStarted = 0;
};

struct State {
  std::unique_ptr<Runtime> runtime;
  Prompt prompt = Prompt::None;
  Prompt previousPrompt = Prompt::None;
  Operation pending = Operation::None;
  gogame::Move pendingMove = gogame::kPass;
  gogame::Move cursor = gogame::kPoints / 2;
  int selected = 0;
  int menuSelected = 0;
  bool loaded = false;
  bool jobActive = false;
  bool discardResult = false;
  bool pendingFromAI = false;
  bool confirmPending = false;
  const char* notice = nullptr;
} state;

static_assert(sizeof(Runtime) + sizeof(State) + 1024 <= 24 * 1024, "Go runtime exceeds the C3 workspace budget");

bool hasTouch() { return board::hasTouch(board::HardwareIdentity::instance().profile()); }
int rowHeight() { return std::max(int(THEME.itemHeight), hasTouch() ? 46 : 30); }
bool finished() { return state.loaded && state.runtime->game.position.outcome != gogame::Outcome::Playing; }
bool whiteTurn() { return state.loaded && !finished() && state.runtime->game.position.toMove == gogame::Color::White; }

void cancelInput(Core& core) {
  state.confirmPending = false;
  core.input.cancelTouchGesture();
  const size_t count = core.events.size();
  for (size_t index = 0; index < count; ++index) {
    Event event;
    if (!core.events.pop(event)) break;
    const bool button = event.type >= EventType::ButtonPress && event.type <= EventType::ButtonCancel;
    if (event.type == EventType::Tap || event.type == EventType::Swipe || (button && event.button != Button::Power))
      continue;
    core.events.push(event);
  }
}

void setPrompt(Core& core, Prompt prompt, int selected = 0) {
  state.prompt = prompt;
  state.selected = selected;
  cancelInput(core);
}

bool stopSearch(Core& core, uint32_t wait = kStopWaitMs) {
  if (!state.runtime) return true;
  core.cpu.unthrottle();
  state.discardResult = true;
  if ((state.jobActive || state.runtime->task.isRunning()) && !state.runtime->task.stop(wait)) return false;
  state.jobActive = false;
  state.runtime->reader.close();
  return true;
}

void restoreBoard(Core& core) {
  state.pending = Operation::None;
  state.pendingFromAI = false;
  setPrompt(core, state.loaded ? Prompt::None : Prompt::Load);
}

bool loadGame(Core& core) {
  hal::Cpu::PerformanceLock performance(core.cpu);
  if (!state.runtime) {
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < sizeof(Runtime)) {
      setPrompt(core, Prompt::Memory);
      return false;
    }
    state.runtime.reset(new (std::nothrow) Runtime);
    if (!state.runtime) {
      setPrompt(core, Prompt::Memory);
      return false;
    }
  }
  const auto result = go_store::load(core.storage, state.runtime->game);
  if (result == go_store::LoadResult::Loaded || (result == go_store::LoadResult::Missing &&
                                                 go_store::startNew(core.storage, state.runtime->game, esp_random()))) {
    state.loaded = true;
    state.notice = nullptr;
    state.pending = Operation::None;
    setPrompt(core, Prompt::None);
    return true;
  }
  state.loaded = false;
  setPrompt(core, Prompt::Load);
  return false;
}

int menu(const char** labels, Command* commands) {
  int count = 0;
  auto add = [&](const char* label, Command command) {
    labels[count] = label;
    commands[count++] = command;
  };
  add(tr(GAME_RESUME), Command::Resume);
  if (!finished() && !whiteTurn()) add(tr(GO_PASS), Command::Pass);
  if (!finished()) add(tr(GO_RESIGN), Command::Resign);
  add(tr(GAME_NEW), Command::New);
  add(tr(GO_SAVE_EXIT), Command::Exit);
  return count;
}

int options(const char** labels, const char*& title) {
  title = "Go 9x9";
  switch (state.prompt) {
    case Prompt::Load:
      title = tr(GAME_LOAD_ERROR);
      labels[0] = tr(RETRY);
      labels[1] = tr(GAME_NEW);
      labels[2] = tr(BACK);
      return 3;
    case Prompt::Memory:
      title = tr(GO_MEMORY_ERROR);
      break;
    case Prompt::Write:
      title = tr(SAVE_FAILED);
      break;
    case Prompt::Think:
      title = tr(GO_THINK_ERROR);
      break;
    case Prompt::StopMenu:
    case Prompt::StopExit:
      title = tr(GO_STOPPING);
      break;
    default: {
      Command commands[5];
      return menu(labels, commands);
    }
  }
  labels[0] = tr(RETRY);
  labels[1] = state.prompt == Prompt::Write || state.prompt == Prompt::StopExit || state.prompt == Prompt::StopMenu
                  ? tr(CANCEL)
                  : tr(BACK);
  return 2;
}

Action openMenu(Core& core) {
  if (!stopSearch(core)) {
    setPrompt(core, Prompt::StopMenu, 1);
    return Action::Redraw;
  }
  restoreBoard(core);
  state.menuSelected = 0;
  return Action::OpenMenu;
}

Action perform(Core& core, bool closeMenu) {
  if (!state.runtime) {
    setPrompt(core, Prompt::Memory);
    return Action::Redraw;
  }
  if (!stopSearch(core)) {
    setPrompt(core, Prompt::StopMenu, 1);
    return Action::Redraw;
  }
  hal::Cpu::PerformanceLock performance(core.cpu);
  bool saved = false;
  if (state.pending == Operation::New) {
    saved = go_store::startNew(core.storage, state.runtime->game, esp_random());
  } else if (state.pending == Operation::Resign) {
    saved = go_store::resign(core.storage, state.runtime->game, gogame::Color::Black);
  } else if (state.pending == Operation::Move) {
    const auto result = go_store::play(core.storage, state.runtime->game, state.pendingMove);
    saved = result == gogame::MoveResult::Ok;
    if (result == gogame::MoveResult::Occupied || result == gogame::MoveResult::Repetition) {
      if (state.pendingFromAI) {
        setPrompt(core, Prompt::Think);
      } else {
        state.notice = result == gogame::MoveResult::Occupied ? tr(GO_OCCUPIED) : tr(GO_REPETITION);
        state.pending = Operation::None;
        cancelInput(core);
      }
      return Action::Redraw;
    }
  }
  if (!saved) {
    setPrompt(core, Prompt::Write, 1);
    return Action::Redraw;
  }
  state.loaded = true;
  state.notice = nullptr;
  if (state.pending == Operation::New) state.cursor = gogame::kPoints / 2;
  restoreBoard(core);
  return closeMenu ? Action::CloseMenu : Action::Redraw;
}

Action place(Core& core, gogame::Move move, bool fromAI = false, bool closeMenu = false) {
  state.pending = Operation::Move;
  state.pendingMove = move;
  state.pendingFromAI = fromAI;
  return perform(core, closeMenu);
}

Action choose(Core& core, int selected, bool menuOpen) {
  switch (state.prompt) {
    case Prompt::Restart:
    case Prompt::Resign:
      if (selected == 0) {
        state.pending = state.prompt == Prompt::Restart ? Operation::New : Operation::Resign;
        state.pendingFromAI = false;
        return perform(core, true);
      }
      restoreBoard(core);
      return Action::Redraw;
    case Prompt::Load:
      if (selected == 0) {
        loadGame(core);
        return Action::Redraw;
      }
      if (selected == 2) return Action::Exit;
      setPrompt(core, Prompt::Restart, 1);
      return Action::Redraw;
    case Prompt::Memory:
      if (selected == 1) return Action::Exit;
      loadGame(core);
      return Action::Redraw;
    case Prompt::Write:
      if (selected == 0) return perform(core, menuOpen);
      {
        const bool wasAI = state.pendingFromAI;
        restoreBoard(core);
        return wasAI ? Action::OpenMenu : Action::Redraw;
      }
    case Prompt::Think:
      if (selected == 1) return openMenu(core);
      if (state.runtime && !state.jobActive && state.runtime->task.getState() == BackgroundTask::State::ERROR) {
        state.runtime.reset();
        state.loaded = false;
        loadGame(core);
      } else {
        restoreBoard(core);
      }
      return Action::Redraw;
    case Prompt::StopMenu:
      if (selected == 0) return openMenu(core);
      restoreBoard(core);
      return Action::Redraw;
    case Prompt::StopExit:
      if (selected == 0) return Action::RetryExit;
      state.prompt = state.previousPrompt;
      state.selected = 1;
      cancelInput(core);
      return Action::CancelExit;
    case Prompt::None:
      break;
  }
  const char* labels[5];
  Command commands[5];
  const int count = menu(labels, commands);
  if (selected < 0 || selected >= count) return Action::None;
  switch (commands[selected]) {
    case Command::Resume:
      state.notice = nullptr;
      return Action::CloseMenu;
    case Command::Pass:
      return place(core, gogame::kPass, false, true);
    case Command::Resign:
      setPrompt(core, Prompt::Resign, 1);
      break;
    case Command::New:
      setPrompt(core, Prompt::Restart, 1);
      break;
    case Command::Exit:
      return Action::Exit;
  }
  return Action::Redraw;
}

Action back(Core& core, bool menuOpen) {
  if (state.prompt == Prompt::StopExit) return choose(core, 1, menuOpen);
  if (state.prompt == Prompt::Load || state.prompt == Prompt::Memory) return Action::Exit;
  if (state.prompt == Prompt::Restart || state.prompt == Prompt::Resign || state.prompt == Prompt::Write ||
      state.prompt == Prompt::StopMenu)
    return choose(core, 1, menuOpen);
  if (menuOpen) return Action::CloseMenu;
  return Action::Exit;
}

bool startSearch(Core& core) {
  Runtime* runtime = state.runtime.get();
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kStackBytes) {
    setPrompt(core, Prompt::Memory);
    return false;
  }
  runtime->result = {gogame::SearchStatus::Cancelled, gogame::kPass, 0};
  state.discardResult = false;
  core.cpu.unthrottle();
  hal::Storage* storage = &core.storage;
  state.jobActive = runtime->task.start(
      "GoOpponent", kStackBytes,
      [runtime, storage]() {
        const auto abort = runtime->task.getAbortCallback();
        const uint32_t started = millis();
        runtime->searchStarted = started;
        if (runtime->reader.open(*storage, runtime->game, abort)) {
          gogame::SearchControl control;
          control.context = runtime;
          control.abort = [](void* context) { return static_cast<Runtime*>(context)->task.shouldStop(); };
          control.yield = [](void*) { vTaskDelay(1); };
          control.budgetExpired = [](void* context) {
            return static_cast<uint32_t>(millis() - static_cast<Runtime*>(context)->searchStarted) >= kSearchBudgetMs;
          };
          const uint32_t seed = runtime->game.seed ^ (runtime->game.position.turns * 0x9e3779b9u);
          runtime->result = runtime->search.run(runtime->game.position, runtime->reader.history(), seed, control);
        } else {
          runtime->result.status = abort() ? gogame::SearchStatus::Cancelled : gogame::SearchStatus::IOError;
        }
        if (!runtime->reader.close() && runtime->result.status == gogame::SearchStatus::Ready)
          runtime->result.status = gogame::SearchStatus::IOError;
        LOG_DBG(kTag, "Opponent: %u simulations, %lu ms, stack free %u bytes", runtime->result.simulations,
                static_cast<unsigned long>(millis() - started),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
      },
      1);
  if (!state.jobActive) setPrompt(core, Prompt::Think);
  return state.jobActive;
}

void drawOptions() {
  const char* labels[5];
  const char* title;
  const int count = options(labels, title);
  renderer.clearScreen(THEME.backgroundColor);
  ui::title(renderer, THEME, THEME.screenMarginTop, title);
  int& selected = state.prompt == Prompt::None ? state.menuSelected : state.selected;
  selected = std::max(0, std::min(selected, count - 1));
  for (int index = 0; index < count; ++index)
    ui::enumValue(renderer, THEME, kRowsTop + index * rowHeight(), labels[index], "", index == selected, rowHeight());
  if (state.prompt == Prompt::Write)
    renderer.drawCenteredText(THEME.smallFontId, kRowsTop + count * rowHeight() + 24, tr(GAME_KEEP_SAVE),
                              THEME.primaryTextBlack);
  ui::buttonBar(renderer, THEME, ui::ButtonBar(tr(BACK), tr(OPEN)));
}
}  // namespace

void enter(Core& core) {
  exit(core);
  state = {};
  cancelInput(core);
  loadGame(core);
}

bool update(Core& core) {
  if (!state.runtime || !state.loaded) {
    core.cpu.throttle();
    return false;
  }
  if (state.jobActive) {
    if (state.runtime->task.isRunning()) {
      core.cpu.unthrottle();
      return false;
    }
    state.runtime->task.stop();
    state.jobActive = false;
    if (!state.discardResult) {
      const auto result = state.runtime->result;
      if (result.status == gogame::SearchStatus::Ready && state.prompt == Prompt::None && whiteTurn()) {
        place(core, result.move, true);
      } else if (result.status != gogame::SearchStatus::Cancelled) {
        setPrompt(core, Prompt::Think);
      }
      return true;
    }
  }
  if (state.prompt == Prompt::None && whiteTurn()) {
    startSearch(core);
    return true;
  }
  core.cpu.throttle();
  return false;
}

Action handleEvent(Core& core, const Event& event, bool menuOpen) {
  Button button = Button::Count;
  const bool modal = state.prompt != Prompt::None;
  if (event.type == EventType::ButtonCancel && event.button == Button::Center) {
    state.confirmPending = false;
    return Action::None;
  }
  if (event.type == EventType::ButtonLongPress && event.button == Button::Center) {
    if (!state.confirmPending) return Action::None;
    state.confirmPending = false;
    return openMenu(core);
  }
  if (event.type == EventType::ButtonRelease && event.button == Button::Center) {
    if (!state.confirmPending) return Action::None;
    state.confirmPending = false;
    if (modal || menuOpen || !state.loaded) return Action::None;
    button = Button::Center;
  } else if (event.type == EventType::ButtonPress || event.type == EventType::ButtonRepeat) {
    button = event.button;
    if (event.type == EventType::ButtonRepeat && button != Button::Up && button != Button::Down &&
        button != Button::Left && button != Button::Right)
      return Action::None;
    if (button == Button::Center && !modal && !menuOpen && state.loaded) {
      state.confirmPending = true;
      return Action::None;
    }
  } else if (event.type == EventType::Tap && hasTouch()) {
    state.confirmPending = false;
    const ui::touch::Point point{event.touch.x, event.touch.y};
    int choice = -1;
    if (state.prompt == Prompt::Restart || state.prompt == Prompt::Resign) {
      const char* title = state.prompt == Prompt::Restart ? tr(GAME_RESTART) : tr(GO_RESIGN_CONFIRM);
      choice = ui::touch::dialogChoiceAt(point, ui::confirmationDialogBounds(renderer, THEME, title));
    } else if (modal || menuOpen) {
      const char* labels[5];
      const char* title;
      const int count = options(labels, title);
      choice = ui::touch::rowAt(point, {0, kRowsTop, int16_t(renderer.getScreenWidth()), int16_t(count * rowHeight())},
                                rowHeight(), count);
    } else if (state.loaded) {
      const auto layout = ui::goLayout(renderer.getScreenWidth(), renderer.getScreenHeight(), true);
      const int pointIndex = ui::goPointAt(layout, point);
      if (pointIndex >= 0) {
        state.cursor = static_cast<gogame::Move>(pointIndex);
        state.notice = nullptr;
        return Action::Redraw;
      }
      if (layout.menu.contains(point)) return openMenu(core);
      if (layout.back.contains(point)) return back(core, false);
      if (layout.place.contains(point))
        button = Button::Center;
      else
        return Action::None;
    }
    if (choice >= 0) return choose(core, choice, menuOpen);
    if (button == Button::Count) {
      if (!miniAppButton(core, event, button)) return Action::None;
      if (button != Button::Back && button != Button::Center) return Action::None;
    }
  } else {
    return Action::None;
  }
  if (button == Button::Back) return back(core, menuOpen);
  if (button == Button::Power || button == Button::Count) return Action::None;
  if (modal || menuOpen) {
    const char* labels[5];
    const char* title;
    const int count = state.prompt == Prompt::Restart || state.prompt == Prompt::Resign ? 2 : options(labels, title);
    int& selected = modal ? state.selected : state.menuSelected;
    if (button == Button::Center) return choose(core, selected, menuOpen);
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
    if (finished()) {
      setPrompt(core, Prompt::Restart, 1);
      return Action::Redraw;
    }
    return whiteTurn() ? Action::None : place(core, state.cursor);
  }
  int column = state.cursor % gogame::kSide;
  int row = state.cursor / gogame::kSide;
  if (button == Button::Left)
    column = std::max(0, column - 1);
  else if (button == Button::Right)
    column = std::min(gogame::kSide - 1, column + 1);
  else if (button == Button::Up)
    row = std::max(0, row - 1);
  else if (button == Button::Down)
    row = std::min(gogame::kSide - 1, row + 1);
  else
    return Action::None;
  state.cursor = static_cast<gogame::Move>(row * gogame::kSide + column);
  state.notice = nullptr;
  return Action::Redraw;
}

miniapps::Frame render(Core& core, bool menuOpen) {
  (void)core;
  if (state.prompt == Prompt::Restart || state.prompt == Prompt::Resign) {
    renderer.clearScreen(THEME.backgroundColor);
    ui::dialog(renderer, THEME, "Go 9x9", state.prompt == Prompt::Restart ? tr(GAME_RESTART) : tr(GO_RESIGN_CONFIRM),
               state.selected);
  } else if (state.prompt != Prompt::None || menuOpen || !state.loaded) {
    drawOptions();
  } else {
    ui::renderGo(renderer, THEME, state.runtime->game.position, state.cursor, hasTouch(), whiteTurn(), state.notice);
  }
  return {miniapps::Refresh::Fast, true};
}

miniapps::ExitResult prepareExit(Core& core, miniapps::ExitTarget target) {
  if (stopSearch(core)) return miniapps::ExitResult::Ready;
  if (state.prompt != Prompt::StopExit) {
    state.previousPrompt = state.prompt;
    setPrompt(core, Prompt::StopExit, 1);
  }
  (void)target;
  return miniapps::ExitResult::Blocked;
}

void exit(Core& core) {
  stopSearch(core, 0);
  state = {};
  cancelInput(core);
  core.cpu.unthrottle();
}

}  // namespace papyrix::go_app
