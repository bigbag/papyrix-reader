#include <RtcBackend.h>
#include <HardwareIdentity.h>
#include <Wire.h>
#include <I2cBus.h>

#include "test_utils.h"

int main() {
  TestUtils::TestRunner runner("RTC backend codec");
  for (uint8_t value = 0; value < 60; ++value) {
    runner.expectEq(value, papyrix::board::bcdToDecimal(papyrix::board::decimalToBcd(value)),
                    "BCD round trip");
  }
  runner.expectEq(uint8_t{23}, papyrix::board::bcdToDecimal(0x23), "BCD hour decode");
  runner.expectEq(uint8_t{0x59}, papyrix::board::decimalToBcd(59), "BCD minute encode");
  using namespace papyrix::board;
  auto& wire = i2cBus(bootProfile().rtc.busIndex);
  wire.reset();
#if PAPYRIX_CAP_RTC && !PAPYRIX_TARGET_XTEINK_C3
  wire.setPresent(0x51, true);
  HardwareIdentity::instance().applyBoardSelection({papyrix::board::kBootBoardId, BoardSelectionSource::Fixed}, {});
#else
  wire.setPresent(0x68, true);
  HardwareIdentity::instance().applyBoardSelection({BoardId::X3, BoardSelectionSource::Override}, {});
#endif
  RtcBackend rtc;
  runner.expectTrue(rtc.begin(), "RTC is available");
#if !PAPYRIX_TARGET_XTEINK_C3
  wire.setRegister(0x51, 0x02, 0x80);
#else
  wire.setRegister(0x68, 0x0F, 0x83);
#endif
  DateTime value{};
  value.year = 2026;
  value.month = 8;
  value.day = 28;
  value.hour = 12;
  value.minute = 34;
  value.second = 56;
  DateTime result{};
  runner.expectFalse(rtc.read(result), "oscillator stop or voltage loss invalidates saved time");
  runner.expectTrue(rtc.write(value), "new time repairs the invalid-time flag");
  runner.expectTrue(rtc.read(result), "saved time is readable after synchronization");
  runner.expectEq(value.second, result.second, "synchronized seconds survive readback");
  runner.expectEq(value.day, result.day, "day uses the controller-specific register order");
  runner.expectEq(value.weekday, result.weekday, "Sunday round-trips across controller-specific encodings");
#if PAPYRIX_TARGET_XTEINK_C3
  runner.expectEq(uint8_t{0x03}, wire.getRegister(0x68, 0x0F), "time synchronization preserves alarm flags");
#endif
  return runner.allPassed() ? 0 : 1;
}
