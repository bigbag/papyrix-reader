#include "core/ReaderButtonDispatcher.h"
#include "test_utils.h"

using namespace papyrix;

int main() {
  TestUtils::TestRunner runner("Reader tap zones");
  ReaderButtonDispatcher dispatcher;
  ReaderButtonConfig config{};
  config.touchPageTurns = true;
  config.logicalWidth = 800;

  runner.expectTrue(dispatcher.processEvent(Event::tap({191, 100}), 0, config) == ReaderButtonAction::Prev,
                    "left 24 percent selects previous");
  runner.expectTrue(dispatcher.processEvent(Event::tap({192, 100}), 0, config) == ReaderButtonAction::Menu,
                    "left boundary belongs to center");
  runner.expectTrue(dispatcher.processEvent(Event::tap({607, 100}), 0, config) == ReaderButtonAction::Menu,
                    "center 52 percent selects menu");
  runner.expectTrue(dispatcher.processEvent(Event::tap({608, 100}), 0, config) == ReaderButtonAction::Next,
                    "right boundary belongs to next");
  runner.expectTrue(dispatcher.processEvent(Event::tap({799, 100}), 0, config) == ReaderButtonAction::Next,
                    "right edge selects next");
  runner.expectTrue(dispatcher.processEvent(Event::tap({800, 100}), 0, config) == ReaderButtonAction::None,
                    "coordinate outside logical width is rejected");

  config.reversePageZones = true;
  runner.expectTrue(dispatcher.processEvent(Event::tap({0, 100}), 0, config) == ReaderButtonAction::Next,
                    "reversed preference swaps left zone");
  runner.expectTrue(dispatcher.processEvent(Event::tap({799, 100}), 0, config) == ReaderButtonAction::Prev,
                    "reversed preference swaps right zone");

  config.reversePageZones = false;
  config.touchPageTurns = false;
  runner.expectTrue(dispatcher.processEvent(Event::tap({0, 100}), 0, config) == ReaderButtonAction::None,
                    "disabled page taps suppress previous");
  runner.expectTrue(dispatcher.processEvent(Event::tap({799, 100}), 0, config) == ReaderButtonAction::None,
                    "disabled page taps suppress next");
  runner.expectTrue(dispatcher.processEvent(Event::tap({400, 100}), 0, config) == ReaderButtonAction::Menu,
                    "center opens menu with page taps disabled");
  runner.expectTrue(dispatcher.processEvent(Event::buttonRelease(Button::Down), 0, config) == ReaderButtonAction::Next,
                    "physical page turn works with page taps disabled");

  config.touchPageTurns = true;
  runner.expectTrue(dispatcher.processEvent(Event::buttonPress(Button::Center), 0, config) == ReaderButtonAction::Menu,
                    "physical button menu access coexists with taps");


  return runner.allPassed() ? 0 : 1;
}
