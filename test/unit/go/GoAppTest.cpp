#include <Arduino.h>
#include <GfxRenderer.h>
#include <HardwareIdentity.h>
#include <ThemeManager.h>
#include <TouchTransform.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cassert>
#include <cstdlib>

#include "GoStoreMock.h"
#include "apps/GoStore.h"
#include "apps/MiniApp.h"
#include "core/Core.h"
#include "game2048_fonts.h"
#include "states/AppLauncherState.h"
#include "test_utils.h"
#include "ui/Elements.h"
#include "ui/views/GoView.h"

using namespace papyrix;
using namespace go_store_test;

GfxRenderer renderer(core.display);

int main() {
  TestUtils::TestRunner runner("Go app lifecycle");
  renderer.begin();
  game2048Fonts(renderer);
  for (const int width : {0, 1, 20, GfxRenderer::BUTTON_HINT_MAX_TEXT_WIDTH}) {
    const auto label = renderer.truncatedText(THEME.uiFontId, "New game", width);
    runner.expectTrue(renderer.getTextWidth(THEME.uiFontId, label.c_str()) <= width,
                      "short footer labels fit their available width");
  }
  runner.expectEq(std::string{"Back"}, renderer.truncatedText(THEME.uiFontId, "Back", 1000),
                  "labels that fit retain their text");
  AppLauncherState launcher(renderer);
  const bool touch = board::hasTouch(board::HardwareIdentity::instance().profile());
  auto blackAt = [&](int x, int y) {
    board::PanelPoint panel{};
    if (!board::panelFromLogical(static_cast<board::DisplayOrientation>(renderer.getOrientation()),
                                 core.display.getDisplayWidth(), core.display.getDisplayHeight(), x, y, panel))
      std::abort();
    const auto byte = renderer.getFrameBuffer()[panel.y * core.display.getDisplayWidthBytes() + panel.x / 8];
    return (byte & (0x80u >> (panel.x % 8))) == 0;
  };
  for (const auto& theme : {getBuiltinLightTheme(), getBuiltinDarkTheme()}) {
    gogame::Position stones;
    stones.cells[40] = gogame::Color::Black;
    stones.cells[41] = gogame::Color::White;
    ui::renderGo(renderer, theme, stones, gogame::kNoMove, touch, false, nullptr);
    const auto layout = ui::goLayout(renderer.getScreenWidth(), renderer.getScreenHeight(), touch);
    const int x = layout.board.x + layout.spacing / 2 + 4 * layout.spacing;
    const int y = layout.board.y + layout.spacing / 2 + 4 * layout.spacing;
    const int radius = layout.spacing * 2 / 5;
    const bool backgroundBlack = theme.backgroundColor == 0;
    runner.expectTrue(blackAt(x, y), "Black stone keeps its black center in both themes");
    runner.expectFalse(blackAt(x + layout.spacing, y), "White stone keeps its white center in both themes");
    for (const int centerX : {x, x + layout.spacing}) {
      bool visibleEdge = false;
      for (int offset = radius / 2; offset <= radius; ++offset)
        visibleEdge |= blackAt(centerX + offset, y + offset) != backgroundBlack;
      runner.expectTrue(visibleEdge, "stone boundary contrasts with the board away from grid lines");
    }
    runner.expectEq(backgroundBlack, blackAt(x + radius, y + radius), "outside the stone retains the theme background");
  }
  auto send = [&](Event event) {
    core.events.push(event);
    const auto result = launcher.update(core);
    launcher.render(core);
    return result;
  };
  auto button = [&](Button value) {
    const auto result = send(Event::buttonPress(value));
    send(Event::buttonRelease(value));
    return result;
  };
  auto menu = [&]() {
    send(Event::buttonPress(Button::Center));
    send(Event::buttonLongPress(Button::Center));
    send(Event::buttonRelease(Button::Center));
  };
  auto open = [&](bool fresh = true) {
    launcher.exit(core);
    if (fresh) reset();
    core.events.clear();
    core.pendingAppId = APP_GO;
    launcher.enter(core);
    launcher.render(core);
  };
  auto saved = [&]() {
    go_store::Game game;
    assert(go_store::load(core.storage, game) == go_store::LoadResult::Loaded);
    return game;
  };

  open();
  runner.expectEq(hal::Display::FAST_REFRESH, core.display.lastRefreshMode,
                  "opening Go avoids an unnecessary full-screen flash");
  runner.expectEq(uint32_t{0}, saved().position.turns, "new game has a complete empty checkpoint");
  send(Event::buttonPress(Button::Center));
  runner.expectEq(uint32_t{0}, saved().position.turns, "Confirm waits for release before placing");
  send(Event::buttonLongPress(Button::Center));
  send(Event::buttonRelease(Button::Center));
  runner.expectEq(uint32_t{0}, saved().position.turns, "holding Confirm opens the menu without placing");
  button(Button::Back);
  send(Event::buttonRelease(Button::Center));
  runner.expectEq(uint32_t{0}, saved().position.turns, "release after closing the menu does not place");
  send(Event::buttonPress(Button::Center));
  send(Event::buttonCancel(Button::Center));
  send(Event::buttonRelease(Button::Center));
  runner.expectEq(uint32_t{0}, saved().position.turns, "cancelled Confirm does not place on release");
  button(Button::Left);
  send(Event::buttonRepeat(Button::Left));
  send(Event::buttonRepeat(Button::Center));
  runner.expectEq(uint32_t{0}, saved().position.turns, "cursor movement and repeated Confirm do not place");
  core.events.push(Event::buttonPress(Button::Center));
  core.events.push(Event::buttonRelease(Button::Center));
  core.events.push(Event::buttonPress(Button::Center));
  core.events.push(Event::buttonRelease(Button::Center));
  launcher.update(core);
  menu();
  const auto first = saved();
  runner.expectEq(uint32_t{1}, first.position.turns, "queued Confirm commits only one human move");
  runner.expectEq(int(gogame::Color::Black), int(first.position.cells[38]), "directions select the committed point");
  runner.expectEq(int(gogame::Color::White), int(first.position.toMove),
                  "menu cancellation retains the pending White turn");
  button(Button::Center);
  runner.expectTrue(launcher.requestSleep(core), "sleep stops the resumed opponent safely");
  launcher.exit(core);
  open(false);
  menu();
  const auto resumed = saved();
  runner.expectTrue(gogame::key(resumed.position) == gogame::key(first.position) &&
                        resumed.position.turns == first.position.turns && resumed.seed == first.seed,
                    "sleep and reopen preserve the complete pending turn");

  button(Button::Down);
  button(Button::Center);
  button(Button::Up);
  button(Button::Center);
  const auto resigned = saved();
  runner.expectEq(int(gogame::Outcome::BlackResigned), int(resigned.position.outcome),
                  "human can resign while the computer has the turn");
  runner.expectEq(first.position.turns, resigned.position.turns, "resignation retains the full move history");
  open(false);
  runner.expectEq(hal::Display::FAST_REFRESH, core.display.lastRefreshMode,
                  "reopening a saved Go game avoids an unnecessary full-screen flash");
  runner.expectEq(int(gogame::Outcome::BlackResigned), int(saved().position.outcome),
                  "out-of-turn resignation survives reopening");

  open();
  getCompleteTaskBeforeCreateReturns() = true;
  button(Button::Center);
  getCompleteTaskBeforeCreateReturns() = false;
  launcher.update(core);
  launcher.render(core);
  const auto computed = saved();
  runner.expectEq(uint32_t{2}, computed.position.turns, "completed opponent result commits exactly one reply");
  runner.expectEq(int(gogame::Color::Black), int(computed.position.toMove), "opponent reply returns control to human");
  open(false);
  runner.expectTrue(gogame::key(computed.position) == gogame::key(saved().position),
                    "real search reply survives app restart");

  open();
  failure = Failure::Sync;
  button(Button::Center);
  failure = Failure::None;
  runner.expectEq(uint32_t{0}, saved().position.turns, "failed move publication keeps the prior game");
  button(Button::Center);
  runner.expectEq(uint32_t{0}, saved().position.turns, "Cancel does not retry a failed move");
  failure = Failure::Write;
  button(Button::Center);
  failure = Failure::None;
  button(Button::Up);
  button(Button::Center);
  button(Button::Back);
  const auto retried = saved();
  runner.expectEq(uint32_t{1}, retried.position.turns, "Retry commits the failed move exactly once");
  runner.expectEq(int(gogame::Color::Black), int(retried.position.cells[40]), "Retry keeps the requested intersection");
  runner.expectEq(int(StateId::Home), int(button(Button::Back).next),
                  "Back leaves Go for Apps, so the next Back reaches Home");
  open(false);
  runner.expectTrue(gogame::key(retried.position) == gogame::key(saved().position),
                    "Back during the opponent turn preserves the saved game");

  open();
  menu();
  button(Button::Down);
  getCompleteTaskBeforeCreateReturns() = true;
  button(Button::Center);
  launcher.update(core);
  launcher.render(core);
  getCompleteTaskBeforeCreateReturns() = false;
  runner.expectTrue(launcher.requestSleep(core), "completed opponent allows sleep");
  const auto passed = saved();
  runner.expectEq(int(gogame::Outcome::Scored), int(passed.position.outcome), "opponent takes a winning second pass");
  runner.expectEq(int(gogame::Color::White), int(gogame::winner(passed.position)),
                  "komi decides the empty-board result");
  open(false);
  runner.expectEq(int(gogame::Outcome::Scored), int(saved().position.outcome), "completed games survive reopening");

  if (touch) {
    for (const uint8_t mapping : {uint8_t(Settings::FrontBCLR), uint8_t(Settings::FrontLRBC)}) {
      core.settings.frontButtonLayout = mapping;
      ui::setFrontButtonLayout(mapping);
      open();
      const auto layout = ui::goLayout(renderer.getScreenWidth(), renderer.getScreenHeight(), true);
      const auto tap = [&](const ui::touch::Rect& rect) {
        return Event::tap({int16_t(rect.x + rect.width / 2), int16_t(rect.y + rect.height / 2)});
      };
      send(Event::tap({int16_t(layout.board.x + layout.spacing / 2), int16_t(layout.board.y + layout.spacing / 2)}));
      runner.expectEq(uint32_t{0}, saved().position.turns, "touch preview does not place a stone");
      send(tap(layout.place));
      send(tap(layout.menu));
      const auto touched = saved();
      runner.expectEq(int(gogame::Color::Black), int(touched.position.cells[0]), "visible Place commits the preview");
      runner.expectEq(uint32_t{1}, touched.position.turns, "touch menu stops search without another move");
      const int footerIndex = mapping == Settings::FrontLRBC ? 2 : 0;
      send(tap(ui::touch::buttonBarButtonRect(footerIndex, renderer.getScreenWidth(), renderer.getScreenHeight())));
      send(tap(layout.back));
      runner.expectEq(int(StateId::Home), int(button(Button::Back).next),
                      "touch Back leaves Go for Apps without an extra menu");
      runner.expectTrue(gogame::key(touched.position) == gogame::key(saved().position),
                        "touch Back preserves the pending computer turn");
    }
    core.settings.frontButtonLayout = Settings::FrontBCLR;
    ui::setFrontButtonLayout(Settings::FrontBCLR);
  }

  launcher.exit(core);
  reset();
  put(finalPath, "invalid save");
  core.pendingAppId = APP_GO;
  launcher.enter(core);
  launcher.render(core);
  runner.expectTrue(*files.at(finalPath) == "invalid save", "opening an invalid save does not reset it");
  button(Button::Down);
  button(Button::Center);
  button(Button::Back);
  runner.expectTrue(*files.at(finalPath) == "invalid save", "cancelled restart preserves invalid data");
  button(Button::Down);
  button(Button::Center);
  button(Button::Up);
  button(Button::Center);
  runner.expectEq(uint32_t{0}, saved().position.turns, "explicit confirmed restart replaces invalid data");

  launcher.exit(core);
  reset();
  testSetLargestFreeBlock(0);
  core.pendingAppId = APP_GO;
  launcher.enter(core);
  launcher.render(core);
  runner.expectTrue(files.empty(), "allocation failure does not change the SD game");
  testResetLargestFreeBlock();
  button(Button::Center);
  runner.expectEq(uint32_t{0}, saved().position.turns, "allocation Retry opens a complete game");

  for (auto* fault : {&testEventCreateFailure, &testMutexCreateFailure}) {
    launcher.exit(core);
    reset();
    core.events.clear();
    core.pendingAppId = APP_GO;
    *fault = true;
    launcher.enter(core);
    *fault = false;
    launcher.render(core);

    menu();
    button(Button::Down);
    button(Button::Center);
    runner.expectEq(uint32_t{1}, saved().position.turns,
                    "menu and move work when task synchronization allocation fails");
    runner.expectTrue(launcher.requestSleep(core), "unstarted opponent allows sleep after allocation failure");
    button(Button::Back);
    runner.expectEq(int(StateId::Home), int(button(Button::Back).next),
                    "Back leaves Go after task synchronization allocation failure");

    open(false);
    getCompleteTaskBeforeCreateReturns() = true;
    launcher.update(core);
    getCompleteTaskBeforeCreateReturns() = false;
    launcher.update(core);
    runner.expectEq(uint32_t{2}, saved().position.turns, "reopening after allocation recovery runs the opponent");
  }

  auto prepareWhiteTurn = [&]() {
    launcher.exit(core);
    reset();
    go_store::Game game;
    runner.expectTrue(go_store::startNew(core.storage, game, 1), "fresh game is saved");
    runner.expectEq(int(gogame::MoveResult::Ok), int(go_store::play(core.storage, game, gogame::kPass)),
                    "Black pass gives White a winning reply");
    core.events.clear();
    core.pendingAppId = APP_GO;
  };

  for (auto* fault : {&testEventCreateFailure, &testMutexCreateFailure}) {
    prepareWhiteTurn();
    *fault = true;
    launcher.enter(core);
    *fault = false;
    launcher.update(core);
    runner.expectEq(uint32_t{1}, saved().position.turns, "failed task initialization leaves White's turn saved");
    getCompleteTaskBeforeCreateReturns() = true;
    button(Button::Center);
    launcher.update(core);
    getCompleteTaskBeforeCreateReturns() = false;
    const auto recovered = saved();
    runner.expectEq(uint32_t{2}, recovered.position.turns, "Think Retry recovers failed task initialization");
    runner.expectEq(int(gogame::Outcome::Scored), int(recovered.position.outcome),
                    "recovered opponent commits the legal winning pass");
  }

  for (const bool sleep : {false, true}) {
    prepareWhiteTurn();
    launcher.enter(core);
    enableEventSetGate();
    launcher.update(core);
    waitForEventSetBlocked();
    testEventWaitFailure = true;
    if (sleep)
      runner.expectFalse(launcher.requestSleep(core), "sleep reports a live worker timeout");
    else
      menu();
    runner.expectEq(uint32_t{1}, saved().position.turns, "timed-out stop does not commit the discarded result");
    testEventWaitFailure = false;
    releaseEventSetGate();
    getTaskRegistry().back()->thread.join();
    getCompleteTaskBeforeCreateReturns() = true;
    button(Button::Center);
    launcher.update(core);
    getCompleteTaskBeforeCreateReturns() = false;
    const auto resumedAfterCancel = saved();
    runner.expectEq(uint32_t{2}, resumedAfterCancel.position.turns,
                    "Cancel after a timed-out stop resumes the White turn");
    runner.expectEq(int(gogame::Outcome::Scored), int(resumedAfterCancel.position.outcome),
                    "resumed opponent commits the legal winning pass");
  }

  open();
  getCompleteTaskBeforeCreateReturns() = true;
  button(Button::Center);
  getCompleteTaskBeforeCreateReturns() = false;
  core.events.push(Event::buttonPress(Button::Center));
  core.events.push(Event::buttonLongPress(Button::Center));
  core.events.push(Event::buttonRelease(Button::Center));
  launcher.update(core);
  runner.expectEq(uint32_t{1}, saved().position.turns, "opening the menu discards an unprocessed opponent result");

  open();
  enableEventSetGate();
  button(Button::Center);
  waitForEventSetBlocked();
  testEventWaitFailure = true;
  runner.expectFalse(launcher.requestSleep(core), "sleep waits for a live opponent that has not stopped");
  runner.expectEq(uint32_t{1}, saved().position.turns, "a blocked stop does not publish the opponent result");
  testEventWaitFailure = false;
  releaseEventSetGate();
  button(Button::Up);
  runner.expectEq(int(StateId::Sleep), int(button(Button::Center).next), "retry sleeps after the opponent stops");
  runner.expectEq(uint32_t{1}, saved().position.turns, "stopping the opponent discards its completed result");
  launcher.exit(core);
  runner.expectEq(0, getForceDeleteCount().load(), "all opponent tasks stop themselves");
  cleanupMockTasks();
  return runner.allPassed() ? 0 : 1;
}
