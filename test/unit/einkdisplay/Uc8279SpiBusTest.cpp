#include <Arduino.h>

#include "Uc8279SpiBus.h"
#include "test_utils.h"

namespace {

bool matches(size_t index, TestGpioEventType type, int pin, int value) {
  if (index >= testGpioEventCount) return false;
  const auto& event = testGpioEvents[index];
  return event.type == type && event.pin == pin && event.value == value;
}

int lowThenHighReadCount = 0;

int alwaysHigh(int) { return HIGH; }
int alwaysLow(int) { return LOW; }
int lowThenHigh(int) { return lowThenHighReadCount++ < 3 ? LOW : HIGH; }

}  // namespace

int main() {
  TestUtils::TestRunner runner("UC8279 SPI bus");
  papyrix::eink::Uc8279SpiBus bus(21, 4, 5, 6, SPISettings(20000000, MSBFIRST, SPI_MODE0));

  SPI.reset();
  testResetGpioEvents();
  bus.command(0x12);
  runner.expectEq(1, static_cast<int>(SPI.beginTransactionCount), "command starts one transaction");
  runner.expectEq(1, static_cast<int>(SPI.endTransactionCount), "command ends one transaction");
  runner.expectEq(1, static_cast<int>(SPI.transferCount), "command transfers one byte");
  runner.expectEq(0x12, static_cast<int>(SPI.transferValues[0]), "command transfers the command byte");
  runner.expectEq(3, static_cast<int>(testGpioEventCount), "command records three GPIO writes");
  runner.expectTrue(matches(0, TestGpioEventType::DigitalWrite, 4, LOW), "command selects command mode");
  runner.expectTrue(matches(1, TestGpioEventType::DigitalWrite, 21, LOW), "command selects the controller");
  runner.expectTrue(matches(2, TestGpioEventType::DigitalWrite, 21, HIGH), "command releases the controller");

  const uint8_t payload[] = {0xAA, 0x55};
  SPI.reset();
  testResetGpioEvents();
  bus.commandData(0x90, payload, sizeof(payload));
  runner.expectEq(1, static_cast<int>(SPI.beginTransactionCount), "command data starts one transaction");
  runner.expectEq(1, static_cast<int>(SPI.endTransactionCount), "command data ends one transaction");
  runner.expectEq(0x90, static_cast<int>(SPI.transferValues[0]), "command data transfers the command byte");
  runner.expectEq(1, static_cast<int>(SPI.writeCount), "command data writes one payload");
  runner.expectEq(2, static_cast<int>(SPI.writeSizes[0]), "command data writes the full payload");
  runner.expectTrue(matches(0, TestGpioEventType::DigitalWrite, 21, LOW), "command data selects the controller");
  runner.expectTrue(matches(1, TestGpioEventType::DigitalWrite, 4, LOW), "command data starts in command mode");
  runner.expectTrue(matches(2, TestGpioEventType::DigitalWrite, 4, HIGH), "command data changes to data mode");
  runner.expectTrue(matches(3, TestGpioEventType::DigitalWrite, 21, HIGH), "command data releases the controller");

  SPI.reset();
  testResetGpioEvents();
  bus.beginData(0x10);
  bus.write(payload, sizeof(payload));
  bus.endData();
  runner.expectEq(2, static_cast<int>(SPI.beginTransactionCount), "stream starts command and data transactions");
  runner.expectEq(2, static_cast<int>(SPI.endTransactionCount), "stream ends command and data transactions");
  runner.expectEq(0x10, static_cast<int>(SPI.transferValues[0]), "stream transfers the command byte");
  runner.expectEq(1, static_cast<int>(SPI.writeCount), "stream writes one payload");
  runner.expectEq(2, static_cast<int>(SPI.writeSizes[0]), "stream writes the full payload");
  runner.expectTrue(matches(3, TestGpioEventType::DigitalWrite, 4, HIGH), "stream selects data mode");
  runner.expectTrue(matches(4, TestGpioEventType::DigitalWrite, 21, LOW), "stream selects the controller for data");
  runner.expectTrue(matches(5, TestGpioEventType::DigitalWrite, 21, HIGH), "stream releases the controller");

  testResetGpioEvents();
  testSetManualMillis(0);
  testSetDigitalReadHook(alwaysHigh);
  runner.expectTrue(bus.waitBusy("no_assert"), "missing BUSY assertion is complete");
  runner.expectTrue(testDelayTotalMs() >= 1000, "missing BUSY assertion has a bounded wait");
  runner.expectFalse(bus.waitBusy("required_assert", true), "required BUSY assertion rejects an idle-high line");

  testResetGpioEvents();
  runner.expectTrue(bus.waitReady("ready"), "idle panel is ready");
  runner.expectEq(uint32_t{0}, testDelayTotalMs(), "ready panel does not wait for a new BUSY assertion");
  testResetGpioEvents();
  testSetManualMillis(0);
  lowThenHighReadCount = 0;
  testSetDigitalReadHook(lowThenHigh);
  runner.expectTrue(bus.waitBusy("complete"), "BUSY low-to-high transition completes");
  lowThenHighReadCount = 0;
  runner.expectTrue(bus.waitReady("ready_after_busy"), "ready check waits for active BUSY to complete");

  testResetGpioEvents();
  testSetManualMillis(0);
  testSetDigitalReadHook(alwaysLow);
  runner.expectFalse(bus.waitBusy("timeout"), "BUSY completion timeout fails");
  runner.expectTrue(testDelayTotalMs() >= 30000, "BUSY completion timeout is bounded");
  runner.expectFalse(bus.waitReady("ready_timeout"), "ready check rejects a stuck BUSY line");

  testSetDigitalReadHook(nullptr);
  testUseRealtimeMillis();

  testResetGpioEvents();
  bus.reset(50);
  runner.expectTrue(matches(0, TestGpioEventType::HoldDisable, 5, 0), "reset releases the RST hold first");
  runner.expectTrue(matches(1, TestGpioEventType::PinMode, 5, OUTPUT), "reset drives RST as output");
  runner.expectTrue(matches(2, TestGpioEventType::DigitalWrite, 5, HIGH), "reset starts HIGH");
  runner.expectTrue(matches(3, TestGpioEventType::Delay, -1, 10), "reset holds HIGH 10 ms");
  runner.expectTrue(matches(4, TestGpioEventType::DigitalWrite, 5, LOW), "reset pulses LOW");
  runner.expectTrue(matches(5, TestGpioEventType::Delay, -1, 10), "reset holds LOW 10 ms");
  runner.expectTrue(matches(6, TestGpioEventType::DigitalWrite, 5, HIGH), "reset releases HIGH");
  runner.expectTrue(matches(7, TestGpioEventType::Delay, -1, 10), "reset settles 10 ms");
  runner.expectTrue(matches(8, TestGpioEventType::Delay, -1, 50), "reset adds the extra settle");

  papyrix::eink::Uc8279SpiBus sticky(15, 16, 17, 18, SPISettings(10000000, MSBFIRST, SPI_MODE0), 20);
  testResetGpioEvents();
  sticky.reset(0);
  runner.expectTrue(matches(4, TestGpioEventType::DigitalWrite, 17, LOW) &&
                        matches(5, TestGpioEventType::Delay, -1, 20) &&
                        matches(6, TestGpioEventType::DigitalWrite, 17, HIGH),
                    "SSD2677 receives a 20 ms reset-low pulse");
  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
