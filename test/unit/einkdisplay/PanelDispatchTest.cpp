#include <Display.h>
#include <HardwareIdentity.h>
#include <PowerPolicy.h>
#include <SPI.h>

#include <algorithm>
#include <cstdlib>

#include "test_utils.h"

namespace {
bool failBusy = false;
unsigned reads = 0;
bool activeHigh = false;
int readBusy(int) {
  testManualMillisValue += 100;
  if (activeHigh) return failBusy ? HIGH : LOW;
  if (failBusy) return HIGH;
  return reads++ % 4 < 2 ? LOW : HIGH;
}
bool sent(uint8_t value) {
  const size_t count = std::min(SPI.transferCount, MockSPI::RECORD_CAPACITY);
  return std::find(SPI.transferValues, SPI.transferValues + count, value) != SPI.transferValues + count;
}
}

int main() {
  TestUtils::TestRunner runner("Display facade dispatch and sleep");
  using papyrix::eink::DisplayController;
  using papyrix::hal::Display;
  auto& identity = papyrix::board::HardwareIdentity::instance();
  Display display;
  testSetManualMillis(100);
  testSetDigitalReadHook(readBusy);
#if PAPYRIX_TARGET_X4CLASSIC || PAPYRIX_TARGET_STICKY
  runner.expectTrue(display.begin() == Display::InitResult::UnsupportedPanel,
                    "unresolved Classic panel cannot initialize");
  runner.expectTrue(display.getFrameBuffer() == nullptr && SPI.transferCount == 0,
                    "unresolved panel allocates no framebuffer and sends no commands");
#endif
#if PAPYRIX_TARGET_STICKY
  const DisplayController panels[] = {DisplayController::SSD1677, DisplayController::SSD2677};
#else
  const DisplayController panels[] = {DisplayController::UC8179_X4PRO, DisplayController::UC8279_X4PRO};
#endif
  for (const auto panel : panels) {
    activeHigh = panel == DisplayController::SSD1677;
    identity.setPanel(panel, papyrix::board::PanelSelectionSource::Probe, 0x68);
    if (display.getFrameBuffer() == nullptr) runner.expectFalse(display.supportsGrayscale(), "uninitialized panel advertises no gray support");
    SPI.reset();
    runner.expectTrue(display.begin() == Display::InitResult::Ok, "facade initializes the selected X4 Pro panel");
    runner.expectTrue(display.supportsGrayscale(), "initialized panel supports grayscale");
    if (panel == DisplayController::SSD2677) {
      runner.expectTrue(sent(0xE9), "SSD2677 receives its own initialization sequence");
      runner.expectEq(12, SPI.misoPin, "SSD2677 preserves shared SD MISO");
    } else if (panel == DisplayController::SSD1677) {
      runner.expectTrue(sent(0x0C), "SSD1677 receives its own booster configuration");
    } else if (panel == DisplayController::UC8279_X4PRO) {
      runner.expectTrue(sent(0x30) == !PAPYRIX_TARGET_X4CLASSIC,
                        "Classic retains factory PLL while Pro programs its PLL");
    } else {
      runner.expectTrue(sent(0x06), "UC8179 retains its booster configuration");
    }
    reads = 0;
    display.displayBuffer(Display::FULL_REFRESH, false);
    if (panel == DisplayController::SSD2677) {
      SPI.reset();
      display.copyGrayscaleBuffers(display.getFrameBuffer(), display.getFrameBuffer());
      display.displayGrayBuffer(false);
      runner.expectTrue(sent(0xFA), "combined planes select absolute OTP gray");
      display.cleanupGrayscaleBuffers(display.getFrameBuffer());
      display.displayBuffer(Display::FULL_REFRESH, false);
      display.copyGrayscaleLsbBuffers(display.getFrameBuffer());
      display.copyGrayscaleMsbBuffers(display.getFrameBuffer());
      SPI.reset();
      display.displayGrayBuffer(false);
      runner.expectTrue(SPI.writeCount == 1 && SPI.writeSizes[0] == 535,
                        "separate masks select overlay AA rather than absolute gray");
      display.cleanupGrayscaleBuffers(display.getFrameBuffer());
    }
    if (panel == DisplayController::SSD1677) {
      display.copyGrayscaleLsbBuffers(display.getFrameBuffer());
      display.copyGrayscaleMsbBuffers(display.getFrameBuffer());
      display.displayGrayBuffer(false);
    } else {
      display.displayBuffer(Display::FAST_REFRESH, false);
    }
    SPI.reset();
    failBusy = true;
    runner.expectFalse(display.deepSleep(), "facade reports a failed power-off handshake");
    runner.expectTrue(panel == DisplayController::SSD1677 ? !sent(0x10) : sent(0x02) && !sent(0x07),
                      "failed power-off cannot send deep sleep");
    SPI.reset();
    failBusy = false;
    reads = 0;
    runner.expectTrue(display.deepSleep(), "facade can complete sleep after BUSY recovers");
    runner.expectTrue(panel == DisplayController::SSD1677 ? sent(0x22) && sent(0x10) : sent(0x02) && sent(0x07),
                      "sleep recovery retries power-off before deep sleep");
#if PAPYRIX_TARGET_STICKY
    const auto& profile = identity.profile();
    SPI.end();
    papyrix::board::shutdownRecoveryRails(profile);
    papyrix::board::restoreRecoveryStorage(profile);
    SPI.begin();  // SdFat starts SPI without pin arguments when it mounts the card.
    runner.expectTrue(SPI.sclkPin == 13 && SPI.misoPin == 12 && SPI.mosiPin == 14 && SPI.selectPin == 8,
                      "SD remount retains Sticky SPI pins after rail recovery");
    reads = 0;
    runner.expectTrue(display.recover() == Display::InitResult::Ok, "display recovers after the SD remount");
    runner.expectTrue(SPI.sclkPin == 13 && SPI.misoPin == 12 && SPI.mosiPin == 14 && SPI.selectPin == 8,
                      "display recovery does not change the active SD bus");
    SPI.reset();
    reads = 0;
    display.displayBuffer(Display::FULL_REFRESH, false);
    runner.expectTrue(sent(panel == DisplayController::SSD1677 ? 0xF7 : 0xEE),
                      "the recovered display can refresh on the shared bus");
#endif
  }
  testSetDigitalReadHook(nullptr);
  testManualMillisEnabled = false;
  std::free(display.getFrameBuffer());
  return runner.allPassed() ? 0 : 1;
}
