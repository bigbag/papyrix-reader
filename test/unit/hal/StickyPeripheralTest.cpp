#include <HardwareIdentity.h>
#include <RtcBackend.h>
#include <Wire.h>
#include <TouchBackend.h>
#include <hal/Battery.h>

#include "test_utils.h"

int main() {
  TestUtils::TestRunner runner("Sticky independent peripherals");
  Wire.reset();
  Wire1.reset();
  Wire.begin(3, 2, 100000);
  Wire.setPresent(0x5D, true);
  Wire.setWideAddress(0x5D);
  papyrix::board::TouchBackend touch;
  runner.expectTrue(touch.begin(papyrix::board::bootProfile()), "Sticky touch initializes");
  Wire1.setPresent(0x55, true);
  Wire1.setRegister16(0x55, 0x2C, 73);
  Wire1.setRegister16(0x55, 0x08, 3890);
  Wire1.setRegister16(0x55, 0x0C, 0);
  papyrix::hal::Battery battery;
  battery.init();
  const auto status = battery.readStatus();
  runner.expectEq(uint16_t{73}, status.percentage, "sensor bus supplies charge level");
  runner.expectEq(uint16_t{3890}, status.millivolts, "sensor bus supplies voltage");
  runner.expectEq(3, Wire.sdaPin(), "gauge polling preserves touch pins");
  runner.expectEq(size_t{0}, Wire.endCount(), "gauge polling keeps touch alive");
  runner.expectEq(size_t{0}, Wire1.endCount(), "gauge leaves the sensor controller active");
  runner.expectFalse(battery.serviceDesignCapacity(), "Sticky never starts calibration");
  runner.expectTrue(battery.finishDesignCapacity(), "no calibration requires closure");
  runner.expectEq(size_t{0}, Wire1.registerWriteCount(0x55, 0x3E), "factory gauge data remains unchanged");
  Wire1.setPresent(0x51, true);
  papyrix::board::RtcBackend rtc;
  runner.expectTrue(rtc.begin(), "RTC uses the independent sensor controller");
  Wire1.setRegister(0x51, 0x02, 0x80);
  papyrix::board::DateTime result{};
  runner.expectFalse(rtc.read(result), "voltage loss invalidates time");
  const papyrix::board::DateTime value{2026, 10, 23, 12, 34, 56, 5};
  runner.expectTrue(rtc.write(value), "synchronization repairs time");
  runner.expectTrue(rtc.read(result), "synchronized time is readable");
  runner.expectEq(uint16_t{2026}, result.year, "year survives synchronization");
  runner.expectEq(uint8_t{10}, result.month, "month survives synchronization");
  runner.expectEq(uint8_t{23}, result.day, "day survives synchronization");
  runner.expectEq(uint8_t{5}, result.weekday, "weekday is not exchanged with day");
  runner.expectEq(uint8_t{12}, result.hour, "hour survives synchronization");
  runner.expectEq(uint8_t{34}, result.minute, "minute survives synchronization");
  runner.expectEq(uint8_t{56}, result.second, "second survives synchronization");
  runner.expectEq(uint8_t{0x23}, Wire1.getRegister(0x51, 0x05), "PCF day register is correct");
  runner.expectEq(uint8_t{5}, Wire1.getRegister(0x51, 0x06), "PCF weekday register is correct");
  Wire1.setReadLimit(2);
  runner.expectFalse(rtc.read(result), "short RTC read is rejected");
  Wire1.setReadLimit(32);
  runner.expectEq(3, Wire.sdaPin(), "RTC preserves touch bus pins");
  Wire.setRegister(0x5D, 0x814E, 0x81);
  Wire.setRegister(0x5D, 0x8150, 100);
  Wire.setRegister(0x5D, 0x8151, 0);
  Wire.setRegister(0x5D, 0x8152, 200);
  Wire.setRegister(0x5D, 0x8153, 0);
  const auto point = touch.poll();
  runner.expectTrue(point.controllerOk && point.fresh && point.contactCount == 1, "sensor access preserves touch");
  runner.expectEq(uint16_t{100}, point.rawX, "byte-zero X is decoded");
  runner.expectEq(uint16_t{200}, point.rawY, "byte-zero Y is decoded");
  touch.shutdown();
  return runner.allPassed() ? 0 : 1;
}
