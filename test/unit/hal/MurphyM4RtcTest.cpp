#include <Arduino.h>
#include <MurphyM4I2c.h>
#include <RtcBackend.h>
#include <Wire.h>
#include <driver/i2c_master.h>

#include "test_utils.h"

using namespace papyrix::board;
namespace bus = papyrix::board::murphy_m4_i2c;
namespace mock = test_native_i2c;

namespace {
bool equal(const DateTime& a, const DateTime& b) {
  return a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour && a.minute == b.minute &&
         a.second == b.second && a.weekday == b.weekday;
}
uint32_t oscillatorStartMs = 0;
uint32_t oscillatorDelayMs = 0;
void waitForOscillator(uint16_t address, uint8_t reg, uint8_t* data, size_t length) {
  if (address == 0x32 && reg == 0x1E && length &&
      static_cast<uint32_t>(millis()) - oscillatorStartMs < oscillatorDelayMs) {
    data[0] |= 0x02;
    mock::registers[0x32][0x1E] |= 0x02;
  }
}
}  // namespace

int main() {
  TestUtils::TestRunner runner("M4 RX8010 and native shared bus");
  uint8_t byte = 0;
  runner.expectFalse(bus::read(bus::Device::Rtc, 0, &byte, 1), "uninitialized reads do not create handles");
  for (unsigned stage : {1u, 2u, 3u}) {
    mock::failNext(stage);
    runner.expectFalse(bus::begin(), "failed bus or device setup is reported");
    runner.expectFalse(mock::bus.active, "partial setup releases its bus");
    for (const auto& device : mock::devices) runner.expectFalse(device.active, "partial setup releases its devices");
    const auto operations = mock::operations;
    runner.expectFalse(bus::read(bus::Device::Touch, 0, &byte, 1), "failed setup prohibits I/O");
    runner.expectEq(operations, mock::operations, "failed setup does not implicitly retry");
  }
  runner.expectTrue(bus::begin(), "explicit setup recovers from each partial failure");
  runner.expectTrue(mock::config.i2c_port == 1 && mock::config.sda_io_num == 13 && mock::config.scl_io_num == 12 &&
                        mock::config.glitch_ignore_cnt == 7 && mock::config.flags.enable_internal_pullup,
                    "shared bus owns the M4 pins and glitch filter");
  const unsigned busCreations = mock::busCreations, deviceCreations = mock::deviceCreations;
  Wire.reset();
  Wire1.reset();
  RtcBackend rtc;
  runner.expectTrue(rtc.begin(), "RX8010 is reachable on native I2C1");
  DateTime value{2026, 10, 23, 12, 34, 56, 5};
  runner.expectTrue(rtc.write(value), "seed a plausible time before voltage loss");
  for (uint8_t reg = 0x17; reg <= 0x1F; ++reg) mock::registers[0x32][reg] = 0xFF;
  for (uint8_t reg = 0x30; reg <= 0x32; ++reg) mock::registers[0x32][reg] = 0xFF;
  runner.expectTrue(rtc.begin(), "voltage-loss recovery initializes an available RTC");
  DateTime beforeSync = value;
  runner.expectFalse(rtc.read(beforeSync), "voltage-loss initialization cannot validate the old stopped time");
  runner.expectTrue(equal(value, beforeSync), "invalid recovery leaves caller time unchanged");
  runner.expectTrue(rtc.write(value), "valid UTC time restarts a voltage-recovered clock");
  runner.expectTrue((mock::registers[0x32][0x1E] & 0x02) == 0 && mock::registers[0x32][0x1F] == 0,
                    "successful recovery clears VLF, STOP, TEST, and interrupt enables");
  runner.expectTrue(mock::registers[0x32][0x17] == 0xD8 && mock::registers[0x32][0x31] == 0x08 &&
                        mock::registers[0x32][0x30] == 0 && mock::registers[0x32][0x32] == 0 &&
                        mock::registers[0x32][0x1D] == 0x04,
                    "voltage recovery restores Epson reserved, offset, extension, and IRQ settings");
  for (uint8_t reg = 0x18; reg <= 0x1C; ++reg)
    runner.expectEq(uint8_t{0}, mock::registers[0x32][reg], "unused alarm and timer data are initialized");
  runner.expectTrue(mock::registers[0x32][0x10] == 0x56 && mock::registers[0x32][0x13] == 0x20 &&
                        mock::registers[0x32][0x14] == 0x23 && mock::registers[0x32][0x16] == 0x26,
                    "saved bytes use BCD and one-hot weekday in the RX8010 register order");
  DateTime result{};
  runner.expectTrue(rtc.read(result) && equal(value, result), "complete UTC value survives a round trip");
  value = {2024, 2, 29, 23, 59, 59, 4};
  runner.expectTrue(rtc.write(value) && rtc.read(result) && equal(value, result), "leap day survives a round trip");
  const DateTime sentinel{2001, 1, 2, 3, 4, 5, 2};
  auto reject = [&]() {
    result = sentinel;
    runner.expectFalse(rtc.read(result), "invalid retained time is rejected");
    runner.expectTrue(equal(result, sentinel), "rejected time does not replace caller data");
  };
  for (const auto& fault : {std::array<uint8_t, 2>{0x1E, 0x02},
                            {0x1F, 0x40},
                            {0x10, 0x6A},
                            {0x10, 0x60},
                            {0x11, 0x60},
                            {0x12, 0x24},
                            {0x13, 0},
                            {0x13, 3},
                            {0x13, 0x80},
                            {0x14, 0},
                            {0x15, 0x13},
                            {0x16, 0x25}}) {
    runner.expectTrue(rtc.write(value), "restore a valid retained value before corruption");
    mock::registers[0x32][fault[0]] = fault[1];
    reject();
  }
  runner.expectTrue(rtc.write(value), "restore leap day");
  mock::failNext();
  reject();
  mock::failNext(2);
  reject();
  mock::shortRead = 3;
  reject();
  for (unsigned stage = 1; stage <= 10; ++stage) {
    mock::registers[0x32][0x1E] = 0xFA;
    mock::failNext(stage);
    runner.expectFalse(rtc.write(value), "each failed recovery transfer reports an unsuccessful time save");
    reject();
    runner.expectTrue(rtc.write(value) && rtc.read(result) && equal(value, result),
                      "a later valid time write recovers from a partial save");
  }
  for (unsigned stage = 1; stage <= 7; ++stage) {
    mock::registers[0x32][0x1E] = 0x02;
    mock::registers[0x32][0x1F] = 0xFF;
    mock::failNext(stage);
    runner.expectFalse(rtc.begin(), "incomplete voltage-loss initialization cannot make the RTC available");
    reject();
    runner.expectTrue(rtc.begin(), "a later begin can recover from incomplete initialization");
    reject();
    runner.expectTrue(rtc.write(value) && rtc.read(result) && equal(value, result),
                      "initialized hardware becomes valid only after a complete time write");
  }
  for (uint32_t start : {100u, 0xFFFFFF00u}) {
    testSetManualMillis(start);
    oscillatorStartMs = start;
    oscillatorDelayMs = 3000;
    mock::registers[0x32][0x1E] = 0x02;
    mock::readHook = waitForOscillator;
    runner.expectTrue(rtc.begin() && rtc.available(), "a rejected first VLF clear does not disable a starting RTC");
    const uint32_t elapsed = static_cast<uint32_t>(millis()) - start;
    runner.expectTrue(elapsed >= 3000 && elapsed <= 3010, "startup permits Epson's complete three-second interval");
    reject();
    runner.expectTrue(rtc.write(value) && rtc.read(result) && equal(value, result),
                      "the first time save succeeds after delayed oscillator startup");
    mock::readHook = nullptr;
  }
  testSetManualMillis(0);
  oscillatorStartMs = 0;
  oscillatorDelayMs = 3001;
  mock::registers[0x32][0x1E] = 0x02;
  mock::readHook = waitForOscillator;
  runner.expectFalse(rtc.begin(), "an oscillator that does not start within the bound leaves RTC unavailable");
  runner.expectTrue(millis() >= 3000 && millis() <= 3010, "an oscillator fault cannot block startup indefinitely");
  reject();
  mock::readHook = nullptr;
  runner.expectTrue(rtc.begin() && rtc.write(value) && rtc.read(result) && equal(value, result),
                    "explicit recovery works after the oscillator fault clears");
  testUseRealtimeMillis();
  for (DateTime invalid : {DateTime{2025, 2, 29}, DateTime{1999, 12, 31}, DateTime{2100, 1, 1}, DateTime{2026, 0, 1},
                           DateTime{2026, 1, 1, 0, 0, 0, 7}}) {
    const auto operations = mock::operations;
    runner.expectFalse(rtc.write(invalid), "invalid input is not saved");
    runner.expectEq(operations, mock::operations, "invalid save performs no device I/O");
  }
  runner.expectTrue(rtc.write(value), "restore valid value for alternating traffic");
  for (unsigned i = 0; i < 20; ++i) {
    runner.expectTrue(bus::read(bus::Device::Touch, 0x02, &byte, 1) && rtc.read(result),
                      "touch and RTC transfers share stable handles");
    runner.expectTrue(bus::begin(), "repeated begin preserves live handles");
  }
  runner.expectEq(busCreations, mock::busCreations, "polling never reallocates the bus");
  runner.expectEq(deviceCreations, mock::deviceCreations, "polling never reallocates devices");
  runner.expectTrue(Wire.beginCount() == 0 && Wire1.beginCount() == 0, "native traffic never claims Wire pins");
  for (const auto& transfer : mock::transfers)
    runner.expectTrue(transfer.timeout == 20 && ((transfer.address == 0x2E && transfer.hz == 100000) ||
                                                 (transfer.address == 0x32 && transfer.hz == 400000)),
                      "each transfer keeps its own device rate and bounded timeout");
  return runner.allPassed() ? 0 : 1;
}
