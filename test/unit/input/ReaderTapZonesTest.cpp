#include "core/ReaderButtonDispatcher.h"
#include "test_utils.h"

using namespace papyrix;

int main() {
  TestUtils::TestRunner runner("Reader tap zones");
  ReaderButtonDispatcher dispatcher;
  ReaderButtonConfig config{};
  config.touchPageTurns = true;
  config.logicalWidth = 480;
  config.logicalHeight = 800;

  struct TapCase {
    int16_t x;
    int16_t y;
    ReaderButtonAction action;
    const char* description;
  };
  const TapCase portrait[] = {
      {0, 749, ReaderButtonAction::Prev, "left zone ends above Back"},
      {0, 750, ReaderButtonAction::Exit, "Back starts at bottom strip"},
      {114, 799, ReaderButtonAction::Exit, "last Back column exits"},
      {115, 799, ReaderButtonAction::None, "bottom center starts after Back"},
      {364, 799, ReaderButtonAction::None, "bottom center ends before Confirm"},
      {365, 799, ReaderButtonAction::Menu, "Confirm starts at right boundary"},
      {240, 749, ReaderButtonAction::Menu, "menu ends above bottom center"},
      {240, 750, ReaderButtonAction::None, "bottom center is inactive"},
      {479, 749, ReaderButtonAction::Next, "right zone ends above Confirm"},
      {-1, 100, ReaderButtonAction::None, "negative x is rejected"},
      {100, -1, ReaderButtonAction::None, "negative y is rejected"},
      {480, 100, ReaderButtonAction::None, "x at screen width is rejected"},
      {100, 800, ReaderButtonAction::None, "y at screen height is rejected"},
  };
  for (const auto& tap : portrait) {
    runner.expectTrue(dispatcher.processEvent(Event::tap({tap.x, tap.y}), 0, config) == tap.action, tap.description);
  }

  config.logicalWidth = 800;
  config.logicalHeight = 480;
  const TapCase landscape[] = {
      {191, 429, ReaderButtonAction::Prev, "landscape previous ends above Back"},
      {192, 429, ReaderButtonAction::Menu, "landscape menu starts at left boundary"},
      {607, 429, ReaderButtonAction::Menu, "landscape menu ends before next"},
      {608, 429, ReaderButtonAction::Next, "landscape next starts at right boundary"},
      {191, 430, ReaderButtonAction::Exit, "landscape Back starts at bottom strip"},
      {192, 430, ReaderButtonAction::None, "landscape bottom center starts after Back"},
      {607, 479, ReaderButtonAction::None, "landscape bottom center ends before Confirm"},
      {608, 430, ReaderButtonAction::Menu, "landscape Confirm starts at bottom strip"},
  };
  for (const auto& tap : landscape) {
    runner.expectTrue(dispatcher.processEvent(Event::tap({tap.x, tap.y}), 0, config) == tap.action, tap.description);
  }

  for (const bool reverse : {false, true}) {
    config.reversePageZones = reverse;
    for (const bool pageTurns : {false, true}) {
      config.touchPageTurns = pageTurns;
      runner.expectTrue(
          dispatcher.processEvent(Event::tap({0, 100}), 0, config) == (!pageTurns ? ReaderButtonAction::None
                                                                       : reverse  ? ReaderButtonAction::Next
                                                                                  : ReaderButtonAction::Prev),
          "page-turn preferences apply to previous");
      runner.expectTrue(
          dispatcher.processEvent(Event::tap({799, 100}), 0, config) == (!pageTurns ? ReaderButtonAction::None
                                                                         : reverse  ? ReaderButtonAction::Prev
                                                                                    : ReaderButtonAction::Next),
          "page-turn preferences apply to next");
      runner.expectTrue(dispatcher.processEvent(Event::tap({0, 479}), 0, config) == ReaderButtonAction::Exit,
                        "page-turn preferences do not change Back");
      runner.expectTrue(dispatcher.processEvent(Event::tap({799, 479}), 0, config) == ReaderButtonAction::Menu,
                        "page-turn preferences do not change Confirm");
      runner.expectTrue(dispatcher.processEvent(Event::tap({400, 100}), 0, config) == ReaderButtonAction::Menu,
                        "page-turn preferences do not change Menu");
      runner.expectTrue(dispatcher.processEvent(Event::tap({400, 479}), 0, config) == ReaderButtonAction::None,
                        "page-turn preferences do not activate bottom center");
    }
  }
  for (const int16_t width : {0, -1}) {
    config.logicalWidth = width;
    runner.expectTrue(dispatcher.processEvent(Event::tap({0, 0}), 0, config) == ReaderButtonAction::None,
                      "nonpositive screen width is rejected");
  }
  config.logicalWidth = 800;
  for (const int16_t height : {0, -1, 50}) {
    config.logicalHeight = height;
    runner.expectTrue(dispatcher.processEvent(Event::tap({0, 0}), 0, config) == ReaderButtonAction::None,
                      "screen without page area is rejected");
  }
  config.logicalHeight = 480;
  config.touchPageTurns = false;
  runner.expectTrue(dispatcher.processEvent(Event::buttonRelease(Button::Down), 0, config) == ReaderButtonAction::Next,
                    "physical page turn works with page taps disabled");

  config.touchPageTurns = true;
  runner.expectTrue(dispatcher.processEvent(Event::buttonPress(Button::Center), 0, config) == ReaderButtonAction::Menu,
                    "physical button menu access coexists with taps");
  runner.expectTrue(dispatcher.processEvent(Event::buttonPress(Button::Back), 0, config) == ReaderButtonAction::Exit,
                    "physical Back still exits");

  return runner.allPassed() ? 0 : 1;
}
