#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <ThemeManager.h>

#include <cassert>

#include "apps/MiniApp.h"
#include "apps/SokobanStore.h"
#include "core/Core.h"
#include "game2048_fonts.h"
#include "sokoban_storage.h"
#include "states/AppLauncherState.h"
#include "test_utils.h"
#include "ui/Elements.h"
#include "ui/views/SokobanView.h"

using namespace papyrix;
using namespace sokoban_test;
GfxRenderer renderer(core.display);

int main() {
  TestUtils::TestRunner runner("Sokoban app lifecycle");
  renderer.begin();
  game2048Fonts(renderer);
  AppLauncherState launcher(renderer);
  auto send = [&](Event event) {
    core.events.push(event);
    const auto result = launcher.update(core);
    launcher.render(core);
    return result;
  };
  auto confirm = [&] {
    send(Event::buttonPress(Button::Center));
    return send(Event::buttonRelease(Button::Center));
  };
  auto menu = [&] {
    send(Event::buttonPress(Button::Center));
    send(Event::buttonLongPress(Button::Center));
    send(Event::buttonRelease(Button::Center));
  };
  auto row = [&](int index) { return Event::tap({100, int16_t(60 + index * 46 + 23)}); };
  auto tap = [&](ui::touch::Rect r) { return Event::tap({int16_t(r.x + r.width / 2), int16_t(r.y + r.height / 2)}); };
  auto yes = [&] {
    const auto bounds = ui::confirmationDialogBounds(renderer, THEME, tr(GAME_RESTART));
    return send(tap(bounds.choices[0]));
  };
  sokoban::Game seed;
  assert(sokoban::start(seed, 0));
  auto open = [&](const sokoban::Game& game) {
    launcher.exit(core);
    reset();
    assert(sokoban::save(core.storage, game));
    core.events.clear();
    core.pendingAppId = APP_SOKOBAN;
    launcher.enter(core);
    launcher.render(core);
  };
  auto snapshot = [&] {
    assert(launcher.requestSleep(core));
    sokoban::Game result;
    assert(sokoban::load(core.storage, result) == sokoban::LoadResult::Loaded);
    return result;
  };

  open(seed);
  runner.expectEq(int(hal::Display::FAST_REFRESH), int(core.display.lastRefreshMode), "board uses fast refresh");
  send(Event::buttonPress(Button::Right));
  send(Event::buttonRepeat(Button::Right));
  send(Event::swipe({100, 200}, Button::Right));
  auto saved = snapshot();
  runner.expectEq(uint32_t{1}, saved.moves, "press moves once; repeat and swipe do not move");
  send(Event::buttonPress(Button::Center));
  send(Event::buttonCancel(Button::Center));
  send(Event::buttonRelease(Button::Center));
  runner.expectEq(uint32_t{1}, snapshot().moves, "canceled confirm does not undo");
  menu();
  send(Event::buttonPress(Button::Back));
  send(Event::buttonRelease(Button::Center));
  runner.expectEq(uint32_t{1}, snapshot().moves, "held confirm and later release do not undo");
  send(Event::buttonPress(Button::Back));
  sokoban::Game reopened;
  assert(sokoban::load(core.storage, reopened) == sokoban::LoadResult::Loaded);
  core.pendingAppId = APP_SOKOBAN;
  launcher.enter(core);
  confirm();
  saved = snapshot();
  runner.expectTrue(saved.moves == 0 && saved.player == seed.player, "undo survives exit and reopen");

  open(seed);
  core.events.push(Event::buttonPress(Button::Center));
  core.events.push(Event::buttonLongPress(Button::Center));
  core.events.push(Event::buttonRelease(Button::Center));
  core.events.push(Event::buttonPress(Button::Down));
  core.events.push(Event::buttonPress(Button::Center));
  launcher.update(core);
  send(Event::buttonPress(Button::Back));
  runner.expectEq(uint32_t{0}, snapshot().moves, "menu transition cancels queued board and menu input");

  open(seed);
  send(Event::buttonPress(Button::Right));
  send(Event::buttonPress(Button::Right));
  send(Event::buttonPress(Button::Down));
  saved = snapshot();
  runner.expectTrue(sokoban::isWon(saved) && saved.moves == 3 && saved.pushes == 1, "push onto goal wins");
  menu();
  send(row(1));
  saved = snapshot();
  runner.expectTrue(!sokoban::isWon(saved) && saved.moves == 2, "menu undo reverses winning push");
  send(Event::buttonPress(Button::Down));
  confirm();
  runner.expectEq(int(1), int(snapshot().level), "confirm advances after win");

  open(seed);
  send(Event::buttonPress(Button::Right));
  menu();
  send(row(2));
  confirm();
  runner.expectEq(uint32_t{1}, snapshot().moves, "restart defaults to No");
  send(row(2));
  failure = Failure::Clear;
  yes();
  send(row(1));
  failure = Failure::None;
  runner.expectEq(uint32_t{1}, snapshot().moves, "failed clear retains RAM game");
  send(row(2));
  yes();
  runner.expectEq(uint32_t{0}, snapshot().moves, "confirmed restart starts the level");

  open(seed);
  menu();
  send(Event::buttonPress(Button::Up));
  send(Event::buttonPress(Button::Up));
  confirm();
  send(Event::buttonPress(Button::Up));
  confirm();
  confirm();
  confirm();
  runner.expectEq(int(0), int(snapshot().level), "level change defaults to No");
  send(Event::buttonPress(Button::Back));
  send(row(3));
  send(Event::buttonPress(Button::Up));
  confirm();
  confirm();
  send(Event::buttonPress(Button::Left));
  confirm();
  runner.expectEq(int(4), int(snapshot().level), "button picker changes page and selects a level");

  sokoban::Game last;
  assert(sokoban::start(last, 11));
  for (const char* p = "LUUULDRDLDUULDD"; *p; ++p) {
    const auto direction = *p == 'L'   ? sokoban::Direction::Left
                           : *p == 'R' ? sokoban::Direction::Right
                           : *p == 'U' ? sokoban::Direction::Up
                                       : sokoban::Direction::Down;
    assert(sokoban::move(last, direction));
  }
  assert(sokoban::isWon(last));
  open(last);
  confirm();
  send(row(0));
  runner.expectEq(int(8), int(snapshot().level), "last solved level opens picker and touch selects a level");

  for (const uint8_t mapping : {uint8_t(Settings::FrontBCLR), uint8_t(Settings::FrontLRBC)}) {
    core.settings.frontButtonLayout = mapping;
    ui::setFrontButtonLayout(mapping);
    open(seed);
    auto layout = ui::sokobanLayout(480, 800, seed, true);
    send(tap(layout.directions[3]));
    send(tap(layout.undo));
    runner.expectEq(uint32_t{0}, snapshot().moves, "touch movement and undo work in either front layout");
    send(tap(layout.menu));
    send(row(3));
    send(row(5));
    send(row(5));
    send(row(3));
    yes();
    runner.expectEq(int(11), int(snapshot().level), "touch picker reaches level 12");
    layout = ui::sokobanLayout(480, 800, snapshot(), true);
    send(tap(layout.back));
    runner.expectEq(int(StateId::Home), int(send(Event::buttonPress(Button::Back)).next), "touch Back exits board");
  }
  core.settings.frontButtonLayout = Settings::FrontBCLR;
  ui::setFrontButtonLayout(Settings::FrontBCLR);

  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Publish}) {
    open(seed);
    send(Event::buttonPress(Button::Right));
    failure = fault;
    runner.expectFalse(launcher.requestSleep(core), "save failure blocks sleep");
    runner.expectFalse(launcher.requestSleep(core), "warning keeps pending sleep blocked");
    failure = Failure::None;
    runner.expectEq(int(StateId::Sleep), int(send(row(0)).next), "Retry resumes sleep destination");
    assert(sokoban::load(core.storage, saved) == sokoban::LoadResult::Loaded);
    runner.expectEq(uint32_t{1}, saved.moves, "retry saves the move");
  }
  open(seed);
  send(Event::buttonPress(Button::Right));
  failure = Failure::Write;
  runner.expectFalse(launcher.requestSleep(core), "failed save blocks sleep before cancel");
  send(row(1));
  failure = Failure::None;
  runner.expectEq(uint32_t{1}, snapshot().moves, "cancel retains unsaved RAM game");
  send(Event::buttonPress(Button::Right));
  failure = Failure::Write;
  runner.expectFalse(launcher.requestSleep(core), "later save failure blocks sleep");
  runner.expectEq(int(StateId::Sleep), int(send(row(2)).next), "discard resumes pending sleep");
  failure = Failure::None;
  assert(sokoban::load(core.storage, saved) == sokoban::LoadResult::Loaded);
  runner.expectEq(uint32_t{1}, saved.moves, "discard preserves prior checkpoint");

  launcher.exit(core);
  reset();
  put(finalPath, "invalid");
  core.pendingAppId = APP_SOKOBAN;
  launcher.enter(core);
  send(row(2));
  runner.expectEqual(std::string("invalid"), *files.at(finalPath), "Back preserves corrupt save");
  core.pendingAppId = APP_SOKOBAN;
  launcher.enter(core);
  send(row(0));
  runner.expectEqual(std::string("invalid"), *files.at(finalPath), "Retry preserves corrupt save");
  send(row(1));
  send(row(0));
  failure = Failure::Clear;
  yes();
  failure = Failure::None;
  send(row(0));
  runner.expectEq(int(0), int(snapshot().level), "retry clear replaces corrupt save after confirmation");
  launcher.exit(core);
  reset();
  return runner.allPassed() ? 0 : 1;
}
