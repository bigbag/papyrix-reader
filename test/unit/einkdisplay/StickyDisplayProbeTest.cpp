#include <Arduino.h>
#include <BoardSelector.h>
#include <HardwareIdentity.h>
#include <SPI.h>

#include "test_utils.h"

namespace {
uint8_t revision = 7;
unsigned bits = 0;
unsigned busyReads = 0;
bool busyHigh = true;
bool unstable = false;
int readPin(int pin) {
  if (pin == 14) return (revision >> (7 - bits++ % 8)) & 1;
  if (pin == 18) return unstable ? busyReads++ % 2 : busyHigh;
  return HIGH;
}
}

int main() {
  using namespace papyrix::board;
  using papyrix::eink::DisplayController;
  TestUtils::TestRunner runner("Sticky panel identification");
  HardwareIdentity identity;
  testSetManualMillis(0);
  testSetDigitalReadHook(readPin);
  SPI.begin(13, 12, 14, 8);
  selectPanel(identity);
  runner.expectTrue(identity.panelResolved() && identity.panel() == static_cast<DisplayController>(6),
                    "revision 07 selects SSD2677");
  runner.expectTrue(identity.panelSource() == PanelSelectionSource::Probe, "known revision is identified");
  runner.expectEq(unsigned{8}, bits, "probe reads one revision byte on the shared data pin");
  runner.expectTrue(SPI.active && SPI.misoPin == 12 && SPI.selectPin == 8, "re-probe restores shared SD bus");
  revision = 0xFF;
  bits = 0;
  busyHigh = false;
  selectPanel(identity);
  runner.expectTrue(identity.panelResolved() && identity.panel() == DisplayController::SSD1677,
                    "stable idle-low supports SSD1677");
  runner.expectTrue(identity.panelSource() == PanelSelectionSource::Fallback, "idle heuristic is reported honestly");
  revision = 0;
  bits = 0;
  busyHigh = true;
  selectPanel(identity);
  runner.expectTrue(identity.panelResolved() && identity.panel() == static_cast<DisplayController>(6),
                    "stable idle-high fallback supports SSD2677");
  unstable = true;
  selectPanel(identity);
  runner.expectFalse(identity.panelResolved(), "unstable BUSY leaves panel unresolved");
  runner.expectTrue(millis() < 1000, "probe remains bounded without a panel response");
  testSetDigitalReadHook(nullptr);
  testUseRealtimeMillis();
  return runner.allPassed() ? 0 : 1;
}
