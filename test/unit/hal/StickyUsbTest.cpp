#include <Wire.h>
#include <hal/Battery.h>
#include <hal/Usb.h>
#include <platform_stubs.h>

#include "test_utils.h"

namespace {
uint32_t adcMv = 0;
}

int main() {
  TestUtils::TestRunner runner("Sticky USB supply");
  Wire1.reset();
  Wire1.setPresent(0x55, true);
  Wire1.setRegister16(0x55, 0x2C, 100);
  Wire1.setRegister16(0x55, 0x08, 4200);
  Wire1.setRegister16(0x55, 0x0C, 0);
  papyrix::hal::Battery battery;
  battery.init();
  const auto full = battery.readStatus();
  runner.expectTrue(full.percentageKnown && full.percentage == 100 && full.chargingKnown && !full.charging,
                    "full gauge has no charge current");
  testAnalogMillivoltsHook() = [](uint8_t pin) -> uint32_t { return pin == 9 ? adcMv : 0; };
  papyrix::hal::Usb usb;
  usb.init(battery);
  adcMv = 2500;
  runner.expectTrue(usb.readStatus().available && usb.readStatus().connected,
                    "full battery on USB remains connected");
  adcMv = 0;
  runner.expectFalse(usb.readStatus().connected, "unplugged device is disconnected");
  adcMv = 1999;
  runner.expectFalse(usb.readStatus().connected, "voltage below threshold is absent");
  adcMv = 2000;
  runner.expectTrue(usb.readStatus().connected, "threshold boundary is present");
  testAnalogMillivoltsHook() = nullptr;
  return runner.allPassed() ? 0 : 1;
}
