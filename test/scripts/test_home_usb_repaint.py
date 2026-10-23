#!/usr/bin/env python3
"""Exercise USB supply edges through the real polling block and Home handlers.

Keep the battery full and unchanged while USB connects and disconnects.
Check that Home repaints on edges, but not on stable input or another screen.
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
#include <HardwareIdentity.h>
#include <hal/UsbPolicy.h>
#include <Logging.h>
constexpr char TAG[] = "USB_TEST";

namespace papyrix {
enum class StateId { Home, Reader };
struct StubBattery {
  struct Status {
    bool percentageKnown = true;
    unsigned percentage = 100;
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
Core core;
class HomeState {
 public:
  struct View {
    bool needsRender = false;
    void setBattery(int) {}
    void setBatteryCharging(bool) {}
  };
  View view_;
  void updateBattery(Core& core);
  void onUsbStateChanged(Core& core);
};
@UPDATE_BATTERY@
@ON_USB_CHANGED@
}  // namespace papyrix

struct {
  bool home = true;
  bool isInState(papyrix::StateId state) const { return home && state == papyrix::StateId::Home; }
} stateMachine;
papyrix::HomeState homeState;
void pollUsb() {
@POLL_USB@
}
int main() {
  pollUsb();
  if (homeState.view_.needsRender) return 1;
  papyrix::core.usb.connected = true;
  pollUsb();
  if (!homeState.view_.needsRender) {
    printf("FAIL: full-battery USB connection does not repaint Home\n");
    return 1;
  }
  homeState.view_.needsRender = false;
  pollUsb();
  if (homeState.view_.needsRender) return 1;
  papyrix::core.usb.connected = false;
  pollUsb();
  if (!homeState.view_.needsRender) {
    printf("FAIL: USB disconnection does not repaint Home\n");
    return 1;
  }
  homeState.view_.needsRender = false;
  stateMachine.home = false;
  papyrix::core.usb.connected = true;
  pollUsb();
  if (homeState.view_.needsRender) return 1;
  printf("PASS: USB edges repaint Home with a full battery; stable input and other screens do not\n");
  return 0;
}
'''


def main():
    home = (ROOT / "src/states/HomeState.cpp").read_text()
    loop = block((ROOT / "src/main.cpp").read_text(), "void loop()")
    start = loop.index("static papyrix::hal::usb_policy::StateTracker")
    declaration_end = loop.index(";", start) + 1
    polling = loop[start:declaration_end] + "\n" + block(loop[declaration_end:], "if (")
    harness = HARNESS.replace("@UPDATE_BATTERY@", block(home, "void HomeState::updateBattery("))
    harness = harness.replace("@ON_USB_CHANGED@", block(home, "void HomeState::onUsbStateChanged("))
    harness = harness.replace("@POLL_USB@", polling)
    with tempfile.TemporaryDirectory(prefix="papyrix-home-usb-") as directory:
        path = Path(directory)
        source = path / "home_usb.cpp"
        source.write_text(harness)
        for target in ("XTEINK_C3", "STICKY"):
            binary = path / target
            command = ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DTEST_BUILD",
                       f"-DPAPYRIX_TARGET_{target}=1"]
            for include in ("test/mocks", "lib/BoardSupport/include", "lib/EInkDisplay/include", "src"):
                command.append("-I" + str(ROOT / include))
            command.extend([str(source), str(ROOT / "lib/BoardSupport/src/HardwareIdentity.cpp"),
                            str(ROOT / "lib/BoardSupport/src/BoardProfiles.cpp"),
                            str(ROOT / "test/mocks/platform_stubs.cpp"), "-o", str(binary)])
            subprocess.run(command, check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            print(target, result.stdout, result.stderr, end="")
            if result.returncode:
                raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
