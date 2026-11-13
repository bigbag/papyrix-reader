#include <Arduino.h>
#include <GfxRenderer.h>
#include <ThemeManager.h>

#include <cassert>

#include "apps/LightsOutStore.h"
#include "apps/MiniApp.h"
#include "core/Core.h"
#include "game2048_fonts.h"
#include "lightsout_storage.h"
#include "states/AppLauncherState.h"
#include "test_utils.h"
#include "ui/Elements.h"
#include "ui/views/LightsOutView.h"

using namespace papyrix;
using namespace lightsout_test;
GfxRenderer renderer(core.display);

int main() {
  TestUtils::TestRunner runner("Lights Out app lifecycle");
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
  auto control = [&](int index) { return Event::tap({int16_t(80 + 160 * index), 767}); };
  const auto seed = lightsout::generate(0x1041);
  auto open = [&](const lightsout::Game& game) {
    launcher.exit(core);
    reset();
    assert(lightsout::save(core.storage, game));
    core.events.clear();
    core.pendingAppId = APP_LIGHTS_OUT;
    launcher.enter(core);
    launcher.render(core);
  };
  auto snapshot = [&] {
    assert(launcher.requestSleep(core));
    lightsout::Game result;
    assert(lightsout::load(core.storage, result) == lightsout::LoadResult::Loaded);
    return result;
  };

  core.pendingAppId = APP_LIGHTS_OUT;
  launcher.enter(core);
  launcher.render(core);
  runner.expectEq(int(hal::Display::FAST_REFRESH), int(core.display.lastRefreshMode),
                  "opening a fresh game avoids a full black-white refresh cycle");
  const auto fresh = snapshot();
  runner.expectTrue(lightsout::valid(fresh) && !lightsout::isWon(fresh), "launcher opens playable Lights Out");

  open(seed);
  runner.expectEq(int(hal::Display::FAST_REFRESH), int(core.display.lastRefreshMode),
                  "reopening a saved game avoids a full black-white refresh cycle");
  send(Event::buttonPress(Button::Center));
  runner.expectEq(uint32_t{0}, snapshot().moves, "Confirm waits for release before toggling");
  send(Event::buttonCancel(Button::Center));
  send(Event::buttonRelease(Button::Center));
  runner.expectEq(uint32_t{0}, snapshot().moves, "canceled Confirm cannot toggle on release");
  menu();
  runner.expectEq(uint32_t{0}, snapshot().moves, "holding Confirm opens the menu without a move");
  send(Event::buttonPress(Button::Back));
  send(Event::buttonRelease(Button::Center));
  runner.expectEq(uint32_t{0}, snapshot().moves, "release after closing the menu does not toggle");
  confirm();
  send(Event::buttonPress(Button::Back));
  lightsout::Game saved;
  assert(lightsout::load(core.storage, saved) == lightsout::LoadResult::Loaded);
  runner.expectEq(uint32_t{1}, saved.moves, "Back saves the current game");
  runner.expectEq(int(StateId::Home), int(send(Event::buttonPress(Button::Back)).next),
                  "Back leaves the board for Apps so the next Back reaches Home");

  auto corner = seed;
  corner.cursor = 0;
  open(corner);
  send(Event::buttonPress(Button::Left));
  send(Event::buttonRepeat(Button::Up));
  saved = snapshot();
  runner.expectTrue(saved.cursor == 0 && saved.moves == 0, "cursor stops at board edges like Go");

  open(seed);
  send(Event::buttonPress(Button::Right));
  send(Event::buttonRepeat(Button::Up));
  confirm();
  send(Event::buttonRepeat(Button::Center));
  send(Event::swipe({100, 200}, Button::Left));
  auto expected = seed;
  assert(lightsout::toggle(expected, 8));
  saved = snapshot();
  runner.expectTrue(saved.lights == expected.lights && saved.moves == 1 && saved.cursor == 8,
                    "directions select and Confirm toggles once without swipe actions");
  const auto record = *files.at(finalPath);
  menu();
  send(Event::buttonPress(Button::Down));
  confirm();
  confirm();
  runner.expectTrue(snapshot().lights == saved.lights, "new-game confirmation defaults to No");
  runner.expectEqual(record, *files.at(finalPath), "canceled restart retains checkpoint");
  confirm();
  send(Event::buttonPress(Button::Left));
  confirm();
  runner.expectTrue(!files.count(finalPath), "confirmed new game removes previous checkpoint");
  runner.expectTrue(!lightsout::isWon(snapshot()), "confirmed new game starts unfinished");

  const auto layout = ui::lightsOutLayout(480, 800, true);
  for (const uint8_t mapping : {uint8_t(Settings::FrontBCLR), uint8_t(Settings::FrontLRBC)}) {
    core.settings.frontButtonLayout = mapping;
    ui::setFrontButtonLayout(mapping);
    open(seed);
    send(Event::tap({int16_t(layout.board.x + layout.cell / 2), int16_t(layout.board.y + layout.cell / 2)}));
    saved = snapshot();
    runner.expectTrue(saved.lights == seed.lights && saved.moves == 0 && saved.cursor == 0,
                      "touch selects a cell without a move, like Go");
    send(control(2));
    send(Event::tap({160, 767}));
    send(control(1));
    send(row(0));
    saved = snapshot();
    expected = seed;
    assert(lightsout::toggle(expected, 0));
    runner.expectTrue(saved.lights == expected.lights && saved.cursor == 0 && saved.moves == 1,
                      "visible Toggle and Menu work in both layouts; control gaps do nothing");
    send(control(0));
    runner.expectEq(int(StateId::Home), int(send(Event::buttonPress(Button::Back)).next),
                    "touch Back returns directly to Apps");
  }
  core.settings.frontButtonLayout = Settings::FrontBCLR;
  ui::setFrontButtonLayout(Settings::FrontBCLR);

  open(seed);
  core.events.push(Event::buttonPress(Button::Center));
  core.events.push(Event::buttonLongPress(Button::Center));
  core.events.push(Event::buttonRelease(Button::Center));
  core.events.push(Event::buttonPress(Button::Down));
  core.events.push(Event::buttonPress(Button::Center));
  launcher.update(core);
  confirm();
  runner.expectTrue(snapshot().lights == seed.lights, "menu transition drops queued board and menu actions");

  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Publish}) {
    open(seed);
    confirm();
    failure = fault;
    runner.expectFalse(launcher.requestSleep(core), "save failure blocks sleep");
    runner.expectFalse(launcher.requestSleep(core), "pending warning blocks repeated sleep");
    failure = Failure::None;
    runner.expectEq(int(StateId::Sleep), int(send(row(0)).next), "Retry finishes pending sleep");
    assert(lightsout::load(core.storage, saved) == lightsout::LoadResult::Loaded);
    runner.expectEq(uint32_t{1}, saved.moves, "retry saves latest move");
  }
  open(seed);
  confirm();
  failure = Failure::Write;
  send(Event::buttonPress(Button::Back));
  send(row(1));
  failure = Failure::None;
  runner.expectEq(uint32_t{1}, snapshot().moves, "canceling a failed Back exit retains the game");
  failure = Failure::Write;
  confirm();
  runner.expectFalse(launcher.requestSleep(core), "later failed save blocks sleep");
  runner.expectEq(int(StateId::Sleep), int(send(row(2)).next), "Discard permits pending sleep");
  failure = Failure::None;
  assert(lightsout::load(core.storage, saved) == lightsout::LoadResult::Loaded);
  runner.expectEq(uint32_t{1}, saved.moves, "Discard retains previous checkpoint");

  auto almostWon = lightsout::generate(1);
  almostWon.cursor = 0;
  open(almostWon);
  confirm();
  saved = snapshot();
  runner.expectTrue(lightsout::isWon(saved) && saved.moves == 1, "last toggle wins and saves solved state");
  open(saved);
  confirm();
  confirm();
  runner.expectTrue(lightsout::isWon(snapshot()), "Confirm on a solved game asks before starting another");
  const uint32_t beforeIdle = millis();
  core.input.resetIdleTimer();
  testSetManualMillis(beforeIdle + 1000);
  launcher.update(core);
  runner.expectEq(uint32_t{1000}, core.input.idleTimeMs(), "idle game permits auto-sleep");
  menu();
  runner.expectEq(int(StateId::Sleep), int(send(Event::buttonLongPress(Button::Power)).next),
                  "Power sleep works from game menu");

  launcher.exit(core);
  reset();
  put(finalPath, "invalid");
  core.pendingAppId = APP_LIGHTS_OUT;
  launcher.enter(core);
  send(row(2));
  runner.expectEqual(std::string("invalid"), *files.at(finalPath), "leaving load error preserves unreadable save");
  core.pendingAppId = APP_LIGHTS_OUT;
  launcher.enter(core);
  send(row(1));
  failure = Failure::Clear;
  send(Event::buttonPress(Button::Left));
  confirm();
  failure = Failure::None;
  send(row(0));
  runner.expectTrue(!lightsout::isWon(snapshot()), "retry failed clear replaces corrupt save with new game");
  launcher.exit(core);
  reset();
  return runner.allPassed() ? 0 : 1;
}
