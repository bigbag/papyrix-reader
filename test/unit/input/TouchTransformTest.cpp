#include <BoardProfile.h>
#include <TouchTransform.h>
#if PAPYRIX_TARGET_MURPHY_M4
#include <MurphyM4Batch.h>
#endif

#include "test_utils.h"

using papyrix::board::DisplayOrientation;
using papyrix::board::PanelPoint;
using papyrix::board::TouchConfig;

namespace {

TouchConfig x4ProTouch() {
  TouchConfig config{};
  config.controller = papyrix::board::TouchController::Gt911;
  config.swapXY = true;
  config.flipY = true;
  config.rawMinX = 0;
  config.rawMaxX = 799;
  config.rawMinY = 0;
  config.rawMaxY = 479;
  return config;
}


void expectRoundTrip(TestUtils::TestRunner& runner, DisplayOrientation orientation, int16_t logicalX,
                     int16_t logicalY, const char* label) {
  PanelPoint panel{};
  PanelPoint logical{};
  const bool forward = papyrix::board::panelFromLogical(orientation, 800, 480, logicalX, logicalY, panel);
  const bool inverse = forward && papyrix::board::logicalFromPanel(orientation, 800, 480, panel.x, panel.y, logical);
  runner.expectTrue(inverse && logical.x == logicalX && logical.y == logicalY, label);
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("Touch coordinate transforms");

  const auto config = x4ProTouch();
  {
    PanelPoint panel{};
    runner.expectTrue(papyrix::board::rawToPanel(config, 0, 0, 800, 480, panel),
                      "raw top-left corner is accepted");
    runner.expectEq(int16_t{0}, panel.x, "swap maps raw Y to panel X");
    runner.expectEq(int16_t{479}, panel.y, "flipY maps raw X to panel bottom");

    runner.expectTrue(papyrix::board::rawToPanel(config, 479, 799, 800, 480, panel),
                      "raw bottom-right corner is accepted");
    runner.expectEq(int16_t{799}, panel.x, "raw maximum maps to panel right");
    runner.expectEq(int16_t{0}, panel.y, "raw maximum maps to flipped panel top");

    runner.expectFalse(papyrix::board::rawToPanel(config, 480, 799, 800, 480, panel),
                       "post-swap Y outside calibration is rejected");
    runner.expectFalse(papyrix::board::rawToPanel(config, 479, 800, 800, 480, panel),
                       "post-swap X outside calibration is rejected");
  }

  auto sticky = config;
  sticky.flipX = true;
  struct Corner { uint16_t rawX, rawY; int16_t x, y; };
  for (const Corner corner : {Corner{0, 0, 799, 479}, Corner{479, 0, 799, 0},
                              Corner{0, 799, 0, 479}, Corner{479, 799, 0, 0}}) {
    PanelPoint panel{};
    runner.expectTrue(papyrix::board::rawToPanel(sticky, corner.rawX, corner.rawY, 800, 480, panel),
                      "Sticky digitizer corner is accepted");
    runner.expectTrue(panel.x == corner.x && panel.y == corner.y, "Sticky corner matches both inverted axes");
  }

  PanelPoint logical{};
  runner.expectTrue(papyrix::board::rawToLogical(sticky, 0, 0, 800, 480, DisplayOrientation::Portrait, logical),
                    "Sticky corner maps to portrait");
  runner.expectTrue(logical.x == 0 && logical.y == 799, "Sticky corner reaches the correct portrait edge");
  runner.expectTrue(papyrix::board::rawToLogical(config, 0, 0, 800, 480, DisplayOrientation::Portrait, logical),
                    "X4 Pro corner maps into portrait coordinates");
  runner.expectEq(int16_t{0}, logical.x, "portrait inverse maps panel bottom to logical left");
  runner.expectEq(int16_t{0}, logical.y, "portrait inverse maps panel left to logical top");
  runner.expectTrue(papyrix::board::rawToLogical(config, 0, 0, 800, 480,
                    DisplayOrientation::LandscapeClockwise, logical),
                    "raw corner maps into clockwise landscape");
  runner.expectEq(int16_t{799}, logical.x, "clockwise landscape maps raw origin to right edge");
  runner.expectEq(int16_t{0}, logical.y, "clockwise landscape maps raw origin to top edge");
  runner.expectTrue(papyrix::board::rawToLogical(config, 0, 0, 800, 480,
                    DisplayOrientation::LandscapeCounterClockwise, logical),
                    "raw corner maps into counter-clockwise landscape");
  runner.expectEq(int16_t{0}, logical.x, "counter-clockwise landscape maps raw origin to left edge");
  runner.expectEq(int16_t{479}, logical.y, "counter-clockwise landscape maps raw origin to bottom edge");
  auto invalidConfig = config;
  invalidConfig.rawMaxX = invalidConfig.rawMinX;
  runner.expectFalse(papyrix::board::rawToLogical(invalidConfig, 0, 0, 800, 480,
                     DisplayOrientation::Portrait, logical), "zero calibration range rejects the sample");

  expectRoundTrip(runner, DisplayOrientation::Portrait, 0, 0, "portrait top-left round-trips");
  expectRoundTrip(runner, DisplayOrientation::Portrait, 479, 799, "portrait bottom-right round-trips");
  expectRoundTrip(runner, DisplayOrientation::LandscapeClockwise, 0, 0, "clockwise landscape top-left round-trips");
  expectRoundTrip(runner, DisplayOrientation::LandscapeClockwise, 799, 479,
                  "clockwise landscape bottom-right round-trips");
  expectRoundTrip(runner, DisplayOrientation::PortraitInverted, 0, 0, "inverted portrait top-left round-trips");
  expectRoundTrip(runner, DisplayOrientation::PortraitInverted, 479, 799,
                  "inverted portrait bottom-right round-trips");
  expectRoundTrip(runner, DisplayOrientation::LandscapeCounterClockwise, 0, 0,
                  "counter-clockwise landscape top-left round-trips");
  expectRoundTrip(runner, DisplayOrientation::LandscapeCounterClockwise, 799, 479,
                  "counter-clockwise landscape bottom-right round-trips");

  int16_t width = 0;
  int16_t height = 0;
  papyrix::board::logicalSize(DisplayOrientation::Portrait, 800, 480, width, height);
  runner.expectEq(int16_t{480}, width, "portrait logical width uses panel height");
  runner.expectEq(int16_t{800}, height, "portrait logical height uses panel width");
  papyrix::board::logicalSize(DisplayOrientation::LandscapeCounterClockwise, 800, 480, width, height);
  runner.expectEq(int16_t{800}, width, "landscape logical width uses panel width");
  runner.expectEq(int16_t{480}, height, "landscape logical height uses panel height");
  const uint8_t revision = papyrix::board::displayOrientationRevision();
  papyrix::board::setDisplayOrientation(DisplayOrientation::LandscapeClockwise);
  runner.expectTrue(papyrix::board::currentDisplayOrientation() == DisplayOrientation::LandscapeClockwise,
                    "renderer orientation is shared with touch input");
  runner.expectTrue(papyrix::board::displayOrientationRevision() != revision,
                    "orientation change advances the shared revision");
  const uint8_t unchangedRevision = papyrix::board::displayOrientationRevision();
  papyrix::board::setDisplayOrientation(DisplayOrientation::LandscapeClockwise);
  runner.expectEq(unchangedRevision, papyrix::board::displayOrientationRevision(),
                  "setting the same orientation keeps the revision stable");
  papyrix::board::setDisplayOrientation(DisplayOrientation::Portrait);

#if PAPYRIX_TARGET_MURPHY_M4
  auto m4 = config;
  m4.controller = papyrix::board::TouchController::Ft6336u;
  for (auto batch : {papyrix::board::MurphyM4Batch::First, papyrix::board::MurphyM4Batch::Second}) {
    papyrix::board::saveMurphyM4Batch(batch);
    papyrix::board::loadMurphyM4Batch();
    const int16_t origin = batch == papyrix::board::MurphyM4Batch::First ? 41 : 40;
    struct Expected {
      DisplayOrientation orientation;
      int16_t x, y;
    };
    for (const auto expected :
         {Expected{DisplayOrientation::Portrait, origin, 0},
          Expected{DisplayOrientation::PortraitInverted, static_cast<int16_t>(479 - origin), 799},
          Expected{DisplayOrientation::LandscapeClockwise, 799, origin},
          Expected{DisplayOrientation::LandscapeCounterClockwise, 0, static_cast<int16_t>(479 - origin)}}) {
      runner.expectTrue(papyrix::board::rawToLogical(m4, 0, 0, 800, 480, expected.orientation, logical) &&
                            logical.x == expected.x && logical.y == expected.y,
                        "M4 origin uses the active batch in every orientation");
      for (auto x : {uint16_t{0}, uint16_t{479}}) {
        for (auto y : {uint16_t{0}, uint16_t{799}}) {
          int16_t w, h;
          papyrix::board::logicalSize(expected.orientation, 800, 480, w, h);
          runner.expectTrue(papyrix::board::rawToLogical(m4, x, y, 800, 480, expected.orientation, logical) &&
                                logical.x >= 0 && logical.x < w && logical.y >= 0 && logical.y < h,
                            "M4 calibrated corner stays inside the active viewport");
        }
      }
    }
    runner.expectFalse(papyrix::board::rawToLogical(m4, 480, 0, 800, 480, DisplayOrientation::Portrait, logical),
                       "M4 rejects invalid raw X before calibration");
    runner.expectFalse(papyrix::board::rawToLogical(m4, 0, 800, 800, 480, DisplayOrientation::Portrait, logical),
                       "M4 rejects invalid raw Y before calibration");
  }
#endif

  return runner.allPassed() ? 0 : 1;
}
