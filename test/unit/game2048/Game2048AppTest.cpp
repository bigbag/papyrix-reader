#include <Arduino.h>
#include <GfxRenderer.h>
#include <ThemeManager.h>

#include <cassert>

#include "apps/Game2048App.h"
#include "apps/Game2048Store.h"
#include "apps/MiniApp.h"
#include "core/Core.h"
#include "game2048_fonts.h"
#include "game2048_storage.h"
#include "states/AppLauncherState.h"
#include "test_utils.h"
#include "ui/Elements.h"
#include "ui/views/Game2048View.h"

using namespace papyrix;
using namespace game2048_test;

GfxRenderer renderer(core.display);

int main() {
  TestUtils::TestRunner runner("2048 app lifecycle");
  renderer.begin();
  game2048Fonts(renderer);
  core.pendingAppId = APP_2048;
  AppLauncherState launcher(renderer);
  launcher.enter(core);
  launcher.render(core);
  runner.expectEq(0, writes, "new game remains in RAM");
  auto send = [&](Event event) {
    core.events.push(event);
    const auto result = launcher.update(core);
    launcher.render(core);
    return result;
  };
  send(Event::buttonPress(Button::Left));
  send(Event::buttonRepeat(Button::Left));
  runner.expectEq(0, writes, "moves never write saves");
  send(Event::buttonPress(Button::Center));
  runner.expectEq(0, writes, "opening menu does not save");
  runner.expectTrue(launcher.requestSleep(core), "sleep saves even from menu");
  game2048::Game saved;
  runner.expectEq(int(game2048::LoadResult::Loaded), int(game2048::load(core.storage, saved)), "sleep snapshot loads");
  launcher.exit(core);
  core.pendingAppId = APP_2048;
  launcher.enter(core);
  send(Event::buttonPress(Button::Back));
  game2048::Game reopened;
  runner.expectEq(int(game2048::LoadResult::Loaded), int(game2048::load(core.storage, reopened)),
                  "saved game survives reopen");
  runner.expectTrue(std::memcmp(saved.cells, reopened.cells, 16) == 0 && saved.score == reopened.score,
                    "reopen preserves every cell and score");
  core.pendingAppId = APP_2048;
  launcher.enter(core);
  send(Event::buttonPress(Button::Center));
  send(Event::buttonPress(Button::Down));
  send(Event::buttonPress(Button::Center));
  send(Event::buttonPress(Button::Back));
  runner.expectEq(int(game2048::LoadResult::Loaded), int(game2048::load(core.storage, reopened)),
                  "cancel restart keeps checkpoint");
  send(Event::buttonPress(Button::Center));
  send(Event::buttonPress(Button::Down));
  send(Event::buttonPress(Button::Center));
  runner.expectEq(int(game2048::LoadResult::Missing), int(game2048::load(core.storage, reopened)),
                  "confirmed restart removes old checkpoint");
  failure = Failure::Write;
  runner.expectFalse(launcher.requestSleep(core), "save failure blocks sleep");
  send(Event::buttonPress(Button::Center));
  failure = Failure::None;
  runner.expectTrue(launcher.requestSleep(core), "cancel then later sleep can save");
  launcher.exit(core);
  const game2048::Game seed{{1, 1}, 0, false};
  auto open = [&](const game2048::Game& game) {
    launcher.exit(core);
    reset();
    assert(game2048::save(core.storage, game));
    writes = 0;
    core.events.clear();
    core.pendingAppId = APP_2048;
    launcher.enter(core);
    launcher.render(core);
  };
  open(seed);
  board::TapClassifier swipeClassifier;
  for (uint32_t elapsed = 0; elapsed <= 120; elapsed += core.cpu.loopDelayMs()) {
    launcher.update(core);
    board::TouchSample sample;
    sample.fresh = true;
    sample.controllerOk = true;
    sample.contactCount = elapsed > 0 && elapsed <= 70 ? 1 : 0;
    sample.position = {int16_t(150 + elapsed), 200};
    const auto frame = swipeClassifier.update(sample, 100 + elapsed, true, false, 0);
    if (frame.swipe) send(Event::swipe({frame.start.x, frame.start.y}, Button::Right));
  }
  runner.expectTrue(launcher.requestSleep(core), "short swipe game saves");
  assert(game2048::load(core.storage, reopened) == game2048::LoadResult::Loaded);
  runner.expectEq(uint32_t{4}, reopened.score, "idle game samples a short swipe before release");
  open(seed);
  runner.expectEq(int(hal::Display::FULL_REFRESH), int(core.display.lastRefreshMode), "entry clears the display");
  auto expectedGame = seed;
  const Button moveButtons[] = {Button::Left, Button::Down, Button::Right, Button::Up};
  const game2048::Direction moveDirections[] = {game2048::Direction::Left, game2048::Direction::Down,
                                                game2048::Direction::Right, game2048::Direction::Up};
  int moves = 0;
  bool fastMoves = true;
  for (int attempt = 0; attempt < 100 && moves < 24; ++attempt) {
    const int direction = attempt % 4;
    if (!game2048::slide(expectedGame, moveDirections[direction])) continue;
    game2048::spawn(expectedGame, 1, 1);
    send(Event::buttonPress(moveButtons[direction]));
    fastMoves = fastMoves && core.display.lastRefreshMode == hal::Display::FAST_REFRESH;
    ++moves;
  }
  runner.expectEq(24, moves, "refresh scenario applies 24 moves");
  runner.expectTrue(fastMoves, "moves do not trigger periodic full refresh");
  auto row = [&](int index) { return Event::tap({100, int16_t(60 + index * 46 + 23)}); };
  auto footer = [&](int index) {
    const int physical = core.settings.frontButtonLayout == Settings::FrontLRBC ? (index + 2) % 4 : index;
    const auto rect = ui::touch::buttonBarButtonRect(physical, 480, 800);
    return Event::tap({int16_t(rect.x + rect.width / 2), int16_t(rect.y + rect.height / 2)});
  };
  const auto layout = ui::game2048Layout(480, 800, true);
  for (int input = 0; input < 3; ++input) {
    open(seed);
    if (input == 0) send(Event::buttonPress(Button::Left));
    if (input == 1) send(Event::tap({int16_t(layout.arrows[0].x + 10), int16_t(layout.arrows[0].y + 10)}));
    if (input == 2) send(Event::swipe({layout.board.x, layout.board.y}, Button::Left));
    runner.expectEq(0, writes, "button tap and swipe moves stay in RAM");
    send(Event::buttonPress(Button::Back));
    assert(game2048::load(core.storage, reopened) == game2048::LoadResult::Loaded);
    runner.expectEq(uint32_t{4}, reopened.score, "each input applies one merge");
    const uint8_t expected[16] = {2, 0, 1};
    runner.expectTrue(std::memcmp(expected, reopened.cells, 16) == 0, "move spawns once after merge");
  }
  open(seed);
  send(Event::buttonRepeat(Button::Left));
  send(Event::swipe({layout.board.x, int16_t(layout.board.y - 1)}, Button::Left));
  send(Event::swipe({layout.arrows[0].x, layout.arrows[0].y}, Button::Left));
  send(footer(2));
  send(footer(3));
  send(footer(0));
  runner.expectEq(0, writes, "repeats outside-board swipes and blank footer slots cannot change game");

  for (const uint8_t mapping : {uint8_t(Settings::FrontBCLR), uint8_t(Settings::FrontLRBC)}) {
    core.settings.frontButtonLayout = mapping;
    ui::setFrontButtonLayout(mapping);
    open(seed);
    send(footer(1));
    send(row(1));
    const auto dialog = ui::confirmationDialogBounds(renderer, THEME, tr(GAME_RESTART));
    const auto no = dialog.choices[1], yes = dialog.choices[0];
    send(Event::tap({int16_t(no.x + 10), int16_t(no.y + 10)}));
    runner.expectEq(0, writes, "touch No leaves checkpoint untouched");
    send(row(1));
    send(Event::tap({int16_t(yes.x + 10), int16_t(yes.y + 10)}));
    runner.expectTrue(!files.count(finalPath) && !files.count(backupPath), "touch Yes clears all old saves");
    runner.expectEq(int(hal::Display::FAST_REFRESH), int(core.display.lastRefreshMode), "restart uses fast refresh");
    send(footer(1));
    send(row(0));
    send(footer(0));
    runner.expectEq(int(game2048::LoadResult::Loaded), int(game2048::load(core.storage, reopened)),
                    "touch-only menu resume and Back save new game in both footer orders");
    runner.expectEq(int(StateId::Home), int(send(Event::buttonPress(Button::Back)).next),
                    "touch Back reaches Apps rather than trapping game");
  }
  core.settings.frontButtonLayout = Settings::FrontBCLR;
  ui::setFrontButtonLayout(Settings::FrontBCLR);

  open(seed);
  core.events.push(Event::buttonPress(Button::Center));
  core.events.push(Event::buttonPress(Button::Left));
  core.events.push(Event::tap({layout.arrows[0].x, layout.arrows[0].y}));
  launcher.update(core);
  send(footer(0));
  send(footer(0));
  runner.expectEq(0, writes, "menu transition drops queued game moves");

  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Publish}) {
    open(seed);
    send(Event::buttonPress(Button::Left));
    send(footer(1));
    failure = fault;
    runner.expectFalse(launcher.requestSleep(core), "menu sleep failure stays interactive");
    const int failedWrites = writes;
    testSetManualMillis(1000000);
    launcher.update(core);
    runner.expectFalse(launcher.requestSleep(core), "pending warning blocks repeated sleep");
    runner.expectEq(failedWrites, writes, "pending sleep does not retry storage before user choice");
    failure = Failure::None;
    runner.expectEq(int(StateId::Sleep), int(send(row(0)).next), "Retry finishes pending menu sleep");
    assert(game2048::load(core.storage, reopened) == game2048::LoadResult::Loaded);
    runner.expectEq(uint32_t{4}, reopened.score, "retry saves latest move");
  }
  open(seed);
  send(Event::buttonPress(Button::Left));
  failure = Failure::Write;
  send(footer(0));
  failure = Failure::None;
  runner.expectEq(int(StateId::AppLauncher), int(send(row(0)).next), "retry Back exits to Apps, not Sleep");
  runner.expectEq(int(StateId::Home), int(send(Event::buttonPress(Button::Back)).next),
                  "retry Back really left the game");

  open(seed);
  send(Event::buttonPress(Button::Left));
  failure = Failure::Write;
  runner.expectFalse(launcher.requestSleep(core), "unsaved game blocks power sleep");
  send(row(1));
  runner.expectEq(uint32_t{0}, core.input.idleTimeMs(), "Cancel restarts inactivity timeout");
  failure = Failure::None;
  runner.expectTrue(launcher.requestSleep(core), "later timeout saves canceled exit game");
  assert(game2048::load(core.storage, reopened) == game2048::LoadResult::Loaded);
  runner.expectEq(uint32_t{4}, reopened.score, "Cancel retains unsaved board in RAM");

  open(seed);
  send(Event::buttonPress(Button::Left));
  failure = Failure::Sync;
  runner.expectFalse(launcher.requestSleep(core), "failed sync opens warning");
  const int beforeDiscard = writes;
  runner.expectEq(int(StateId::Sleep), int(send(row(2)).next), "explicit Discard permits sleep");
  launcher.exit(core);
  runner.expectEq(beforeDiscard, writes, "exit after Discard never writes again");
  failure = Failure::None;
  assert(game2048::load(core.storage, reopened) == game2048::LoadResult::Loaded);
  runner.expectEq(uint32_t{0}, reopened.score, "Discard keeps previous complete checkpoint");

  open(seed);
  core.input.resetIdleTimer();
  const uint32_t beforeIdle = millis();
  testSetManualMillis(beforeIdle + 1000);
  launcher.update(core);
  runner.expectEq(uint32_t{1000}, core.input.idleTimeMs(), "ordinary game does not prevent auto-sleep");
  send(footer(1));
  testSetManualMillis(beforeIdle + 2000);
  launcher.update(core);
  runner.expectEq(uint32_t{2000}, core.input.idleTimeMs(), "game menu does not prevent auto-sleep");
  runner.expectEq(int(StateId::Sleep), int(send(Event::buttonLongPress(Button::Power)).next),
                  "long Power uses save gate from game menu");

  open(game2048::Game{{10, 10, 1}, 0, false});
  send(Event::buttonPress(Button::Left));
  send(row(0));
  send(footer(0));
  assert(game2048::load(core.storage, reopened) == game2048::LoadResult::Loaded);
  runner.expectTrue(reopened.wonAcknowledged && reopened.cells[0] == 11 && reopened.score == 2048,
                    "Continue after win remains resumable");

  const game2048::Game almostOver{{1, 1, 3, 4, 3, 4, 5, 6, 4, 5, 6, 7, 5, 6, 7, 8}, 0, false};
  open(almostOver);
  put(backupPath, *files.at(finalPath));
  send(Event::buttonPress(Button::Left));
  runner.expectTrue(!files.count(finalPath) && !files.count(backupPath), "game over removes earlier checkpoint");
  send(footer(0));
  runner.expectEq(0, writes, "game-over exit cannot save ended game");

  open(almostOver);
  failure = Failure::Clear;
  send(Event::buttonPress(Button::Left));
  runner.expectTrue(files.count(finalPath) != 0, "clear failure does not claim checkpoint vanished");
  runner.expectFalse(launcher.requestSleep(core), "game-over clear failure blocks sleep");
  failure = Failure::None;
  runner.expectEq(int(StateId::Sleep), int(send(row(0)).next), "retry clears ended game before sleep");
  runner.expectTrue(!files.count(finalPath), "cleared game cannot resume old checkpoint");

  open(seed);
  send(footer(1));
  send(row(1));
  failure = Failure::Clear;
  send(Event::buttonPress(Button::Left));
  send(Event::buttonPress(Button::Center));
  failure = Failure::None;
  send(row(1));
  send(footer(0));
  send(footer(0));
  assert(game2048::load(core.storage, reopened) == game2048::LoadResult::Loaded);
  runner.expectTrue(std::memcmp(seed.cells, reopened.cells, 16) == 0, "cancel failed restart retains original board");

  open(seed);
  send(Event::buttonPress(Button::Left));
  send(footer(1));
  send(row(1));
  failure = Failure::Write;
  runner.expectFalse(launcher.requestSleep(core), "Power can interrupt restart confirmation");
  send(row(1));
  failure = Failure::None;
  send(Event::buttonPress(Button::Center));
  runner.expectTrue(files.count(finalPath) != 0, "Cancel sleep keeps restart confirmation on No");

  launcher.exit(core);
  reset();
  put(finalPath, "invalid");
  core.pendingAppId = APP_2048;
  launcher.enter(core);
  send(footer(0));
  runner.expectEqual("invalid", files.at(finalPath)->c_str(), "leaving load error preserves unreadable save");
  runner.expectEq(0, writes, "load error cannot silently replace save");
  launcher.exit(core);
  return runner.allPassed() ? 0 : 1;
}
