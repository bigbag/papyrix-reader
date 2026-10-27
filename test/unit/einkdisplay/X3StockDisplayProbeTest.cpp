#include <Arduino.h>
#include <X3DisplayProbe.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace {
constexpr papyrix::eink::X3DisplayProbePins pins{8, 10, 21, 4, 5, 6};
std::array<int, 32> levels{}, modes{};
std::array<uint8_t, 3> reply{};
std::vector<unsigned long> resetTimes;
std::vector<int> resetLevels;
unsigned long readyAt = 0;
unsigned samples = 0;
unsigned commands = 0;
unsigned commandBits = 0;
unsigned holds = 0;
uint8_t command = 0;

void consumeEvents() {
  for (size_t i = 0; i < testGpioEventCount; ++i) {
    const auto& event = testGpioEvents[i];
    if (event.type == TestGpioEventType::PinMode) modes.at(event.pin) = event.value;
    if (event.type == TestGpioEventType::DigitalWrite) levels.at(event.pin) = event.value;
    if (event.type == TestGpioEventType::HoldDisable && event.pin == pins.reset) ++holds;
  }
  testGpioEventCount = 0;
}

void writePin(int pin, int value) {
  const int previous = levels.at(pin);
  consumeEvents();
  if (pin == pins.reset) {
    resetTimes.push_back(millis());
    resetLevels.push_back(value);
  }
  if (pin == pins.cs && value == LOW) {
    command = 0;
    commandBits = 0;
  }
  if (pin == pins.sclk && previous == LOW && value == HIGH && levels[pins.cs] == LOW && levels[pins.dc] == LOW &&
      modes[pins.sda] == OUTPUT) {
    command = static_cast<uint8_t>((command << 1) | levels[pins.sda]);
    if (++commandBits == 8) ++commands;
  }
}

int readPin(int pin) {
  consumeEvents();
  if (pin == pins.busy) return millis() >= readyAt ? HIGH : LOW;
  assert(pin == pins.sda && command == 0x70 && commandBits == 8);
  assert(levels[pins.cs] == LOW && levels[pins.dc] == HIGH);
  assert(levels[pins.sclk] == HIGH && modes[pins.sda] == INPUT);
  assert(millis() >= 110 && samples < 24);
  const int bit = (reply[samples / 8] >> (7 - samples % 8)) & 1;
  ++samples;
  return bit;
}

void prepare(std::array<uint8_t, 3> bytes, unsigned long busyUntil) {
  reply = bytes;
  readyAt = busyUntil;
  samples = commands = commandBits = holds = 0;
  command = 0;
  levels.fill(HIGH);
  modes.fill(INPUT);
  resetTimes.clear();
  resetLevels.clear();
  testSetManualMillis(0);
  testResetGpioEvents();
}

void checkPins() {
  consumeEvents();
  assert(commands == 1 && samples == 24);
  assert(resetTimes == (std::vector<unsigned long>{0, 10, 60}));
  assert(resetLevels == (std::vector<int>{HIGH, LOW, HIGH}));
  assert(holds == 1);
  assert(modes[pins.sclk] == INPUT && modes[pins.sda] == INPUT);
  assert(modes[pins.cs] == INPUT_PULLUP && levels[pins.cs] == HIGH);
  assert(modes[pins.dc] == INPUT && modes[pins.reset] == INPUT && modes[pins.busy] == INPUT);
}
}  // namespace

int main() {
  using namespace papyrix::eink;
  testSetDigitalReadHook(readPin);
  testSetDigitalWriteHook(writePin);

  for (const auto& bytes : {std::array<uint8_t, 3>{0, 0, 0x66}, std::array<uint8_t, 3>{0xFF, 0xFF, 0x66}}) {
    for (const unsigned long busyUntil : {0UL, 230UL, 10000UL}) {
      prepare(bytes, busyUntil);
      const auto report = probeX3DisplayController(pins);
      assert(report.verdict == X3DisplayVerdict::UC8279Confirmed);
      assert(std::equal(bytes.begin(), bytes.end(), report.ver));
      assert(report.busyTimedOut == (busyUntil == 10000));
      assert(millis() == (busyUntil == 0 ? 111UL : busyUntil == 230 ? 230UL : 410UL));
      checkPins();
    }
  }

  for (unsigned id = 0; id < 256; ++id) {
    prepare({0xFF, 0xFF, static_cast<uint8_t>(id)}, 0);
    const auto report = probeX3DisplayController(pins);
    const auto expected = id == 0x66   ? X3DisplayVerdict::UC8279Confirmed
                          : id == 0xFF ? X3DisplayVerdict::UC8253StableDefault
                                       : X3DisplayVerdict::Inconclusive;
    assert(report.verdict == expected);
    checkPins();
  }

  testSetDigitalReadHook(nullptr);
  testSetDigitalWriteHook(nullptr);
  testUseRealtimeMillis();
  std::puts("X3 stock probe: ID selection, clock phase, BUSY bounds, reset and pin release passed");
}
