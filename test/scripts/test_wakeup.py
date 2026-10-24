#!/usr/bin/env python3
"""Run the firmware wake classifier with recorded ESP wake causes."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <cassert>
#include <esp_system.h>
enum esp_sleep_wakeup_cause_t {
  ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_GPIO,
  ESP_SLEEP_WAKEUP_EXT1, ESP_SLEEP_WAKEUP_TIMER
};
esp_sleep_wakeup_cause_t cause;
auto esp_sleep_get_wakeup_cause() { return cause; }
namespace papyrix {
struct Usb { bool connected; bool isConnected() const { return connected; } };
struct Core { Usb usb; } core;
}
'''

CASES = r'''
int main() {
  papyrix::core.usb.connected = true;
  testResetReason = ESP_RST_DEEPSLEEP;
  cause = ESP_SLEEP_WAKEUP_EXT1;
  assert(getWakeupInfo().isPowerButton);
  assert(!getWakeupInfo().usbColdBoot);
  cause = ESP_SLEEP_WAKEUP_GPIO;
  assert(getWakeupInfo().isPowerButton);
  papyrix::core.usb.connected = false;
  assert(getWakeupInfo().isPowerButton);
  cause = ESP_SLEEP_WAKEUP_EXT1;
  assert(getWakeupInfo().isPowerButton);
  assert(!getWakeupInfo().usbColdBoot);
  cause = ESP_SLEEP_WAKEUP_TIMER;
  assert(!getWakeupInfo().isPowerButton);
  cause = ESP_SLEEP_WAKEUP_UNDEFINED;
  assert(!getWakeupInfo().isPowerButton);
  papyrix::core.usb.connected = true;
  cause = ESP_SLEEP_WAKEUP_TIMER;
  assert(!getWakeupInfo().isPowerButton);
  cause = ESP_SLEEP_WAKEUP_UNDEFINED;
  testResetReason = ESP_RST_POWERON;
  assert(!getWakeupInfo().isPowerButton);
  assert(getWakeupInfo().usbColdBoot);
  papyrix::core.usb.connected = false;
  assert(getWakeupInfo().isPowerButton);
  assert(!getWakeupInfo().usbColdBoot);
  testResetReason = ESP_RST_SW;
  assert(!getWakeupInfo().isPowerButton);
  assert(!getWakeupInfo().usbColdBoot);
}
'''


def main():
    source = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "src/main.cpp").read_text()
    start = source.index("struct WakeupInfo {")
    function = source.index("WakeupInfo getWakeupInfo()", start)
    end = source.index("\n}", function) + 2
    with tempfile.TemporaryDirectory(prefix="papyrix-wakeup-") as directory:
        path = Path(directory)
        (path / "wake.cpp").write_text(HARNESS + source[start:end] + CASES)
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", f"-I{ROOT / 'test/mocks'}",
            str(path / "wake.cpp"), "-o", str(path / "wake"),
        ], check=True)
        subprocess.run([str(path / "wake")], check=True)
    print("PASS: GPIO/EXT1 button wake, USB cold boot, timer wake, and software reset")


if __name__ == "__main__":
    main()
