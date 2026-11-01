#include <Display.h>
#include <SPI.h>

#include <array>
#include <cstring>
#include <vector>

#include "test_utils.h"

namespace {
constexpr size_t rowBytes = 100;
using Plane = std::array<uint8_t, 48000>;
Plane bw{}, red{}, glass{}, referenceAtActivation{};
std::vector<uint8_t> parameters;
uint8_t command = 0, update = 0;
uint16_t xStart = 0, xEnd = 799, x = 0, yStart = 479, yEnd = 0, y = 479;
bool dataMode = false, busyFailure = false;
bool temperatureLoaded = false, waveformLoaded = false;

void output(int pin, int value) {
  if (pin == 4) dataMode = value == HIGH;
}

void transfer(const uint8_t* bytes, size_t size) {
  if (!dataMode) {
    command = bytes[0];
    parameters.clear();
    if (command == 0x12) temperatureLoaded = waveformLoaded = false;
    if (command == 0x20) {
      if (update & 0x20) temperatureLoaded = true;
      if (update & 0x10) waveformLoaded = temperatureLoaded;
    }
    if (command == 0x20 && (update & 0x04) && waveformLoaded) {
      referenceAtActivation = red;
      glass = bw;
      // Model a controller that exchanges its RAM planes after activation.
      bw.swap(red);
    }
    return;
  }
  if (command == 0x24 || command == 0x26) {
    auto& plane = command == 0x24 ? bw : red;
    for (size_t i = 0; i < size; ++i) {
      plane.at((479 - y) * rowBytes + x / 8) = bytes[i];
      x += 8;
      if (x > xEnd) {
        x = xStart;
        y = y > yEnd ? y - 1 : yStart;
      }
    }
    return;
  }
  parameters.insert(parameters.end(), bytes, bytes + size);
  if (command == 0x44 && parameters.size() == 4) {
    xStart = parameters[0] | (parameters[1] << 8);
    xEnd = parameters[2] | (parameters[3] << 8);
  } else if (command == 0x45 && parameters.size() == 4) {
    yStart = parameters[0] | (parameters[1] << 8);
    yEnd = parameters[2] | (parameters[3] << 8);
  } else if (command == 0x4E && parameters.size() == 2) {
    x = parameters[0] | (parameters[1] << 8);
  } else if (command == 0x4F && parameters.size() == 2) {
    y = parameters[0] | (parameters[1] << 8);
  } else if (command == 0x22) {
    update = parameters[0];
  } else if (command == 0x1A) {
    temperatureLoaded = true;
  } else if (command == 0x32) {
    waveformLoaded = true;
  } else if (command == 0x46) {
    bw.fill(0xFF);
  } else if (command == 0x47) {
    red.fill(0xFF);
  }
}

int busy(int pin) { return pin == 6 ? (busyFailure ? HIGH : LOW) : HIGH; }
}  // namespace

int main() {
  using papyrix::hal::Display;
  TestUtils::TestRunner runner("X4 SSD1677 frame retention");
  testSetManualMillis(0);
  testSetDigitalWriteHook(output);
  testSetDigitalReadHook(busy);
  testSetSpiWriteHook(transfer);
  Display display;
  runner.expectTrue(display.begin() == Display::InitResult::Ok, "X4 display initializes");
  runner.expectTrue(SPI.lastClockHz <= 10000000, "X4 transfers stay within the reference SPI limit");
  auto* frame = display.getFrameBuffer();
  Plane boot{}, home{};
  boot.fill(0xA5);
  for (size_t i = 0; i < home.size(); ++i) home[i] = static_cast<uint8_t>(i * 13 + i / rowBytes);
  std::memcpy(frame, boot.data(), boot.size());
  display.displayBuffer(Display::FULL_REFRESH, false);
  std::memcpy(frame, home.data(), home.size());
  display.displayBuffer(Display::FAST_REFRESH, false);
  runner.expectTrue(glass == home, "Home replaces the boot image");
  runner.expectTrue(bw == home && red == home, "both RAM planes retain Home after activation");

  Plane window = home;
  for (size_t row = 0; row < 3; ++row) {
    const size_t source = (7 + row) * rowBytes + 1;
    frame[source] = window[source] = 0x3C;
    frame[source + 1] = window[source + 1] = 0xC3;
  }
  display.displayWindow(8, 7, 16, 3, true);
  runner.expectTrue(glass == window, "a small update cannot restore Booting outside the window");
  runner.expectTrue(bw == window && red == window, "a window keeps both RAM planes synchronized");

  std::memcpy(frame, home.data(), home.size());
  display.displayBuffer(Display::HALF_REFRESH, false);
  display.displayBufferDriveAll(false);
  runner.expectTrue(glass == home && bw == home && red == home,
                    "drive-all leaves the displayed image ready for a window update");

  busyFailure = true;
  display.displayBuffer(Display::FAST_REFRESH, true);
  busyFailure = false;
  std::memcpy(frame, boot.data(), boot.size());
  display.displayWindow(8, 7, 16, 3, true);
  runner.expectTrue(glass == boot && bw == boot && red == boot,
                    "a failed refresh requires whole-frame recovery before a window update");

  std::memcpy(frame, home.data(), home.size());
  display.displayBuffer(Display::FAST_REFRESH, true);
  runner.expectTrue(display.lastRefreshSucceeded(), "the last image completes before standby");
  runner.expectTrue(display.deepSleep(), "the panel enters standby after a confirmed image");
  Display resumed;
  runner.expectTrue(resumed.begin() == Display::InitResult::Ok, "a new MCU session initializes the panel");
  std::memcpy(resumed.getFrameBuffer(), home.data(), home.size());
  runner.expectTrue(resumed.restoreMonochromeBaseline() == Display::BaselineRestoreResult::Ready,
                    "the previous image restores without a full refresh");
  runner.expectTrue(glass == home && bw == home && red == home,
                    "RAM restoration preserves the glass and both reference planes");
  std::memcpy(resumed.getFrameBuffer(), boot.data(), boot.size());
  resumed.displayBuffer(Display::FAST_REFRESH, true);
  runner.expectTrue(resumed.lastRefreshSucceeded() && glass == boot && referenceAtActivation == home,
                    "the first resumed update compares against the previous physical image");
  busyFailure = true;
  resumed.displayBuffer(Display::FAST_REFRESH, true);
  runner.expectFalse(resumed.lastRefreshSucceeded(), "a timeout cannot retain an earlier successful result");
  busyFailure = false;
  std::memcpy(resumed.getFrameBuffer(), home.data(), home.size());
  resumed.displayBuffer(Display::FAST_REFRESH, true);
  runner.expectTrue(resumed.lastRefreshSucceeded() && glass == home && referenceAtActivation == home,
                    "an uncertain image requires a complete baseline before further updates");
  busyFailure = true;
  Display failed;
  runner.expectTrue(failed.begin() == Display::InitResult::HardwareError, "initialization reports a controller timeout");
  runner.expectTrue(failed.restoreMonochromeBaseline() == Display::BaselineRestoreResult::Error,
                    "failed initialization cannot restore a baseline");
  busyFailure = false;
  testSetSpiWriteHook(nullptr);
  testSetDigitalReadHook(nullptr);
  testSetDigitalWriteHook(nullptr);
  testUseRealtimeMillis();
  return runner.allPassed() ? 0 : 1;
}
