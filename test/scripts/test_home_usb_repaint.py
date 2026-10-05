#!/usr/bin/env python3
"""Exercise HomeState USB-edge repaint with host substitutes.

Extracts the real HomeState::updateBattery() and
HomeState::onUsbStateChanged() bodies and calls the edge handler with
unchanged battery values. The repaint flag must flip false to true.
"""

import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <cstdio>

namespace papyrix {

struct StubBattery {
  struct Status {
    bool percentageKnown = true;
    unsigned percentage = 50;
  };
  Status readStatus() const { return {}; }
};

struct StubUsb {
  bool connected = false;
  bool isConnected() const { return connected; }
};

struct Core {
  StubBattery battery;
  StubUsb usb;
};

class HomeState {
 public:
  struct SpyView {
    bool needsRender = false;
    int batteryCalls = 0;
    int chargingCalls = 0;
    void setBattery(int) { ++batteryCalls; }
    void setBatteryCharging(bool) { ++chargingCalls; }
  };
  SpyView view_;
  void updateBattery(Core& core);
  void onUsbStateChanged(Core& core);
};

@UPDATE_BATTERY@
@ON_USB_CHANGED@
}  // namespace papyrix

int main() {
  papyrix::Core core;
  papyrix::HomeState state;
  state.view_.needsRender = false;
  state.onUsbStateChanged(core);
  if (state.view_.batteryCalls != 1 || state.view_.chargingCalls != 1) {
    printf("FAIL: USB edge skipped the battery update\n");
    return 1;
  }
  if (!state.view_.needsRender) {
    printf("FAIL: USB edge did not request a full Home repaint\n");
    return 1;
  }
  printf("PASS: USB edge requests a full Home repaint with unchanged battery\n");
  return 0;
}
'''


def main():
    home = (ROOT / "src/states/HomeState.cpp").read_text()
    harness = HARNESS.replace("@UPDATE_BATTERY@", block(home, "void HomeState::updateBattery("))
    harness = harness.replace("@ON_USB_CHANGED@", block(home, "void HomeState::onUsbStateChanged("))
    with tempfile.TemporaryDirectory(prefix="papyrix-home-usb-") as directory:
        path = Path(directory)
        source = path / "home_usb.cpp"
        source.write_text(harness)
        binary = path / "home_usb"
        subprocess.run(
            ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
            check=True,
        )
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if result.returncode:
            print("FAIL: Home USB repaint", flush=True)
            raise SystemExit(1)
        print("PASS: Home USB repaint", flush=True)


if __name__ == "__main__":
    main()
