#include <Display.h>
#include <Ft6336uBackend.h>
#include <HardwareIdentity.h>
#include <InputManager.h>
#include <MappedInputManager.h>
#include <MurphyM4Batch.h>
#include <RtcBackend.h>
#include <SPI.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <vector>

#include "MurphyM4TouchFixture.h"
#include "hal/Input.h"
#include "test_utils.h"

InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);

namespace {
std::atomic<bool> resetLow{false};
std::atomic<bool> poweredOff{false};
int corruptRegister = -1;
void checkRead(uint16_t address, uint8_t reg, uint8_t* data, size_t length) {
  if (address != 0x2E) return;
  assert(!resetLow.load() && !poweredOff.load());
  if (reg == corruptRegister && length == 1) data[0] ^= 1;
}
void output(int pin, int value) {
  if (pin == 7) resetLow = value == LOW;
  if (pin == 45) poweredOff = value == HIGH;
}
bool wrote(int pin, int value) {
  for (size_t i = 0; i < testGpioEventCount; ++i)
    if (testGpioEvents[i].type == TestGpioEventType::DigitalWrite && testGpioEvents[i].pin == pin &&
        testGpioEvents[i].value == value)
      return true;
  return false;
}
}  // namespace

int main() {
  using namespace papyrix;
  using namespace papyrix::board;
  using Frame = MurphyM4TouchFixture::Frame;
  TestUtils::TestRunner runner("M4 native touch-to-event integration");
  MurphyM4TouchFixture fixture;
  testSetDigitalReadHook([](int pin) { return pin == 8 ? LOW : HIGH; });
  testSetDigitalWriteHook(output);
  test_native_i2c::readHook = checkRead;
  const auto& profile = HardwareIdentity::instance().profile();
  loadMurphyM4Batch();
  RawTouchSample decoded{};
  uint8_t truncated[10]{};
  runner.expectFalse(decodeFt6336uFrame(truncated, sizeof(truncated), decoded), "truncated frame is rejected");
  runner.expectFalse(decodeFt6336uFrame(nullptr, 11, decoded), "null frame is rejected");
  std::vector<Frame> invalid;
  Frame allFf;
  allFf.fill(0xFF);
  invalid.push_back(allFf);
  invalid.push_back(MurphyM4TouchFixture::frame(0x10));
  invalid.push_back(MurphyM4TouchFixture::frame(3));
  invalid.push_back(MurphyM4TouchFixture::frame(1, 100, 200, 3));
  invalid.push_back(MurphyM4TouchFixture::frame(1, 480, 200));
  invalid.push_back(MurphyM4TouchFixture::frame(1, 100, 800));
  for (const auto& bytes : invalid)
    runner.expectFalse(decodeFt6336uFrame(bytes.data(), bytes.size(), decoded), "invalid frame cannot reach input");
  auto idle = MurphyM4TouchFixture::frame(0);
  runner.expectTrue(decodeFt6336uFrame(idle.data(), idle.size(), decoded) && decoded.contactCount == 0,
                    "factory idle encoding releases input");
  auto release = MurphyM4TouchFixture::frame(1, 100, 200, 1);
  runner.expectTrue(decodeFt6336uFrame(release.data(), release.size(), decoded) && decoded.contactCount == 0,
                    "single-contact release event is idle");
  auto releaseWithoutCoordinates = MurphyM4TouchFixture::frame(1, 4095, 4095, 1);
  runner.expectTrue(decodeFt6336uFrame(releaseWithoutCoordinates.data(), releaseWithoutCoordinates.size(), decoded) &&
                        decoded.contactCount == 0,
                    "a release event does not require active-contact coordinates");
  auto multiRelease = MurphyM4TouchFixture::frame(2, 100, 200, 1);
  runner.expectTrue(decodeFt6336uFrame(multiRelease.data(), multiRelease.size(), decoded) && decoded.contactCount == 2,
                    "two contacts cancel even when the first contact reports release");

  testResetDelayStats();
  testResetGpioEvents();
  enableTouch(profile.touch);
  runner.expectTrue(wrote(45, LOW) && testDelayTotalMs() == 500 && !wrote(7, LOW),
                    "touch power waits 500 ms without resetting the shared display");
  testResetDelayStats();
  testResetGpioEvents();
  hal::Display display;
  runner.expectTrue(display.begin() == hal::Display::InitResult::Ok, "display initializes before touch acquisition");
  size_t resetWrites = 0;
  bool resetTiming = true;
  for (size_t i = 0; i < testGpioEventCount; ++i) {
    if (testGpioEvents[i].type != TestGpioEventType::DigitalWrite || testGpioEvents[i].pin != 7) continue;
    resetTiming = resetTiming && testGpioEvents[i].value == (resetWrites == 0 ? LOW : HIGH) &&
                  i + 1 < testGpioEventCount && testGpioEvents[i + 1].type == TestGpioEventType::Delay &&
                  testGpioEvents[i + 1].value == (resetWrites == 0 ? 50 : 100);
    ++resetWrites;
  }
  runner.expectTrue(resetWrites == 2 && resetTiming, "M4 display owns a 50 ms low and 100 ms high reset");
  auto& owner = ft6336uBackend();
  for (int reg : {0x00, 0x80, 0x88}) {
    corruptRegister = reg;
    runner.expectFalse(owner.begin(profile), "runtime register readback is required before task startup");
    runner.expectFalse(owner.available(), "failed readback leaves touch unavailable");
  }
  corruptRegister = -1;
  for (unsigned offset = 1; offset <= 7; ++offset) {
    test_native_i2c::failNext(offset);
    runner.expectFalse(owner.begin(profile), "each failed startup transfer leaves acquisition stopped");
  }
  MurphyM4TouchFixture::setFrame(allFf);
  runner.expectFalse(owner.begin(profile), "invalid startup probe cannot expose touch input");
  MurphyM4TouchFixture::setFrame(idle);
  testTaskCreateFailureName = "m4-touch";
  runner.expectFalse(owner.begin(profile), "failed task creation cannot fall back to main-loop polling");
  runner.expectFalse(owner.available(), "failed capture task leaves touch unavailable");
  testTaskCreateFailureName = "Buttons";
  testSetHeapPool(MALLOC_CAP_INTERNAL, 4095, 4095);
  runner.expectFalse(owner.begin(profile), "capture cannot start without a complete internal stack block");
  testResetLargestFreeBlock();
  inputManager.begin();
  EventQueue queue;
  hal::Input input;
  testSetManualMillis(0);
  const auto configurationStart = test_native_i2c::transfers.size();
  runner.expectTrue(input.init(queue).ok(), "HAL initializes the actual native acquisition owner");
  fixture.waitPaused();
  for (size_t i = 0; i < 7; ++i) {
    const auto& transfer = test_native_i2c::transfers[configurationStart + i];
    const uint8_t expected[] = {0x00, 0x80, 0x88, 0x00, 0x80, 0x88, 0x02};
    runner.expectTrue(transfer.address == 0x2E && transfer.hz == 100000 && transfer.reg == expected[i] &&
                          transfer.write == (i < 3) && transfer.length == (i == 6 ? 11u : 1u),
                      "startup writes all runtime registers before readback and the complete frame probe");
  }
  runner.expectTrue(test_native_i2c::registers[0x2E][0x00] == 0 && test_native_i2c::registers[0x2E][0x80] == 0x16 &&
                        test_native_i2c::registers[0x2E][0x88] == 0x04,
                    "runtime configuration uses the donor values");
  setDisplayOrientation(DisplayOrientation::LandscapeCounterClockwise);
  fixture.step(0, idle);
  input.poll();
  queue.clear();
  const auto buses = test_native_i2c::busCreations, devices = test_native_i2c::deviceCreations;
  auto drain = [&](uint32_t now) {
    testSetManualMillis(now);
    input.poll();
    std::vector<Event> events;
    Event event{};
    while (queue.pop(event)) events.push_back(event);
    return events;
  };
  fixture.step(10, MurphyM4TouchFixture::frame(1, 0, 123));
  fixture.step(85, idle);
  fixture.step(100, MurphyM4TouchFixture::frame(1, 479, 700));
  fixture.step(175, idle);
  auto events = drain(500);
  runner.expectTrue(
      events.size() == 1 && events[0].type == EventType::Tap && events[0].touch.x == 123 && events[0].touch.y == 439,
      "blocked main loop receives the first complete short contact exactly once");
  runner.expectTrue(drain(510).empty(), "a later buffered contact cannot replace or repeat the first");
  suppressTouchUntilIdle();
  fixture.step(520, MurphyM4TouchFixture::frame(1, 0, 123));
  fixture.step(595, idle);
  suppressTouchUntilIdle();
  suppressTouchUntilIdle();
  events = drain(599);
  runner.expectTrue(events.size() == 1 && events[0].type == EventType::Tap,
                    "multipass replay starts with an unread idle sample in the same bounded HAL poll");

  auto noTap = [&](uint32_t now) {
    const auto observed = drain(now);
    runner.expectTrue(observed.empty(), "cancelled gesture emits no Tap, Confirm, or Home");
  };
  fixture.step(600, MurphyM4TouchFixture::frame(1, 100, 200));
  fixture.step(610, MurphyM4TouchFixture::frame(1, 200, 200));
  fixture.step(620, MurphyM4TouchFixture::frame(1, 100, 200));
  fixture.step(685, idle);
  noTap(900);
  fixture.step(1000, MurphyM4TouchFixture::frame(1));
  fixture.step(1010, multiRelease);
  fixture.step(1020, MurphyM4TouchFixture::frame(1));
  fixture.step(1085, idle);
  noTap(1200);
  fixture.step(1300, MurphyM4TouchFixture::frame(1));
  fixture.step(1310, allFf);
  fixture.step(1385, idle);
  noTap(1500);
  fixture.step(1600, MurphyM4TouchFixture::frame(1));
  test_native_i2c::failNext();
  fixture.step(1699, idle);
  noTap(1699);
  test_native_i2c::failNext();
  fixture.step(1700, idle);
  noTap(1800);
  fixture.step(1810, MurphyM4TouchFixture::frame(1));
  fixture.step(1885, idle);
  noTap(1900);

  fixture.step(2000, idle);
  suppressTouchUntilIdle();
  fixture.step(2010, MurphyM4TouchFixture::frame(1, 0, 123));
  fixture.step(2085, idle);
  suppressTouchUntilIdle();
  fixture.step(2100, idle);
  suppressTouchUntilIdle();
  fixture.step(2110, idle);
  events = drain(2500);
  runner.expectTrue(events.size() == 1 && events[0].type == EventType::Tap,
                    "completed contact survives successive monochrome and grayscale refresh suppression");
  runner.expectTrue(drain(2510).empty(), "successive render passes do not repeat the retained tap");
  fixture.step(2600, MurphyM4TouchFixture::frame(1));
  suppressTouchUntilIdle();
  fixture.step(2610, MurphyM4TouchFixture::frame(1));
  fixture.step(2685, idle);
  noTap(2800);
  fixture.step(2900, MurphyM4TouchFixture::frame(1));
  setDisplayOrientation(DisplayOrientation::Portrait);
  fixture.step(2910, MurphyM4TouchFixture::frame(1));
  fixture.step(2985, idle);
  noTap(3100);
  fixture.step(3120, MurphyM4TouchFixture::frame(1));
  fixture.step(3195, idle);
  setDisplayOrientation(DisplayOrientation::LandscapeCounterClockwise);
  noTap(3250);
  fixture.step(0xFFFFFFF0u, MurphyM4TouchFixture::frame(1));
  fixture.step(0x3Bu, idle);
  events = drain(0x100u);
  runner.expectTrue(events.size() == 1 && events[0].type == EventType::Tap,
                    "a 75 ms contact survives acquisition timestamp wrap");
  fixture.step(3300, idle);
  fixture.step(3310, MurphyM4TouchFixture::frame(1));
  fixture.step(3385, idle);
  input.resyncState();
  runner.expectTrue(drain(3500).empty(), "input remapping discards a completed contact from the previous layout");
  uint32_t contactStart = 3600;
  for (bool acquisitionFirst : {false, true}) {
    fixture.step(contactStart - 10, idle);
    fixture.step(contactStart, MurphyM4TouchFixture::frame(1, 0, 123));
    fixture.step(contactStart + 75, idle);
    fixture.step(contactStart + 150, MurphyM4TouchFixture::frame(1, 100, 200));
    fixture.step(contactStart + 160, MurphyM4TouchFixture::frame(1, 200, 200));
    fixture.step(contactStart + 170, MurphyM4TouchFixture::frame(1, 100, 200));
    suppressTouchUntilIdle();
    if (acquisitionFirst) fixture.step(contactStart + 180, MurphyM4TouchFixture::frame(1, 100, 200));
    events = drain(contactStart + 200);
    runner.expectTrue(
        events.size() == 1 && events[0].type == EventType::Tap && events[0].touch.x == 123 && events[0].touch.y == 439,
        "refresh preserves the first completed tap while a later contact is held");
    runner.expectTrue(drain(contactStart + 210).empty(), "held later contact cannot repeat the retained tap");
    fixture.step(contactStart + 250, idle);
    runner.expectTrue(drain(contactStart + 300).empty(),
                      "interrupted later contact cannot become a tap when polling wins the acquisition race");
    fixture.step(contactStart + 400, MurphyM4TouchFixture::frame(1, 0, 345));
    fixture.step(contactStart + 475, idle);
    events = drain(contactStart + 500);
    runner.expectTrue(
        events.size() == 1 && events[0].type == EventType::Tap && events[0].touch.x == 345 && events[0].touch.y == 439,
        "an idle release permits a new contact after the interrupted contact");
    contactStart += 700;
  }

  for (uint32_t start : {5100u, 0xFFFFFFF0u}) {
    fixture.step(start - 10, idle);
    drain(start - 10);
    for (uint32_t elapsed = 0; elapsed <= 180; elapsed += 10)
      fixture.step(start + elapsed, MurphyM4TouchFixture::frame(1, 0, 123));
    test_native_i2c::failNext();
    fixture.step(start + 190, idle);
    runner.expectTrue(drain(start + 190).empty(), "a transient failed read does not finish a held contact");
    fixture.step(start + 200, idle);
    events = drain(start + 250);
    runner.expectTrue(
        events.size() == 1 && events[0].type == EventType::Tap && events[0].touch.x == 123 && events[0].touch.y == 439,
        "stationary valid samples prevent false stale cancellation after a brief I2C failure");
    runner.expectTrue(drain(start + 260).empty(), "transient-failure replay emits its completed tap only once");
  }

  struct RecoveryCase {
    uint32_t gapMs;
    bool held;
    bool tap;
  };
  for (uint32_t first : {1000u, 0xFFFFFFA0u}) {
    uint32_t start = first;
    for (const auto recovery :
         {RecoveryCase{110, false, false}, {99, false, true}, {100, false, false}, {110, true, false}}) {
      fixture.step(start - 10, idle);
      drain(start - 10);
      fixture.step(start, MurphyM4TouchFixture::frame(1, 0, 123));
      test_native_i2c::failNext();
      fixture.step(start + 99, idle);
      fixture.step(start + recovery.gapMs, recovery.held ? MurphyM4TouchFixture::frame(1, 0, 123) : idle);
      if (recovery.held) fixture.step(start + recovery.gapMs + 10, idle);
      events = drain(start + recovery.gapMs + 25);
      runner.expectTrue(recovery.tap ? events.size() == 1 && events[0].type == EventType::Tap &&
                                           events[0].touch.x == 123 && events[0].touch.y == 439
                                     : events.empty(),
                        "recovered input uses the previous valid sample's 100 ms expiry boundary");
      fixture.step(start + recovery.gapMs + 100, MurphyM4TouchFixture::frame(1, 0, 345));
      fixture.step(start + recovery.gapMs + 175, idle);
      events = drain(start + recovery.gapMs + 200);
      runner.expectTrue(events.size() == 1 && events[0].type == EventType::Tap && events[0].touch.x == 345 &&
                            events[0].touch.y == 439,
                        "release after recovered input permits a new valid contact");
      start += 400;
    }
  }

  const uint32_t validDownAt = static_cast<uint32_t>(millis()) + 20;
  fixture.step(validDownAt - 10, idle);
  drain(validDownAt - 10);
  fixture.step(validDownAt, MurphyM4TouchFixture::frame(1, 257, 257, 0));
  fixture.step(validDownAt + 10, MurphyM4TouchFixture::frame(1, 257, 257));
  fixture.step(validDownAt + 75, idle);
  events = drain(validDownAt + 100);
  runner.expectTrue(
      events.size() == 1 && events[0].type == EventType::Tap && events[0].touch.x == 257 && events[0].touch.y == 220,
      "DOWN at raw (257,257) produces a tap at its calibrated position");
  runner.expectTrue(drain(validDownAt + 110).empty(), "the valid DOWN contact emits only one tap");

  fixture.releaseForStop();
  runner.expectTrue(display.recover() == hal::Display::InitResult::Ok,
                    "display recovery restores volatile touch config");
  fixture.stopComplete();
  fixture.waitPaused();
  runner.expectTrue(test_native_i2c::busCreations == buses && test_native_i2c::deviceCreations == devices,
                    "shared reset preserves native RTC and touch handles");
  noTap(static_cast<uint32_t>(millis()));
  RtcBackend rtc;
  runner.expectTrue(rtc.begin() && rtc.write({2026, 10, 23, 12, 34, 56, 5}), "RTC holds a valid UTC value");
  for (int i = 0; i < 16; ++i) {
    fixture.step(static_cast<uint32_t>(millis()) + 10, MurphyM4TouchFixture::frame(1));
    fixture.releaseForStop();
    DateTime utc;
    runner.expectTrue(rtc.read(utc) && utc.year == 2026 && utc.month == 10 && utc.day == 23 && utc.hour == 12 &&
                          utc.minute == 34 && utc.second == 56 && utc.weekday == 5,
                      "RTC reads stay valid while the independent touch task runs");
    runner.expectTrue(display.recover() == hal::Display::InitResult::Ok, "shared reset repeatedly restores touch");
    fixture.stopComplete();
    fixture.waitPaused();
    fixture.step(static_cast<uint32_t>(millis()) + 10, idle);
    noTap(static_cast<uint32_t>(millis()));
  }
  runner.expectTrue(test_native_i2c::busCreations == buses && test_native_i2c::deviceCreations == devices,
                    "repeated RTC traffic and display recovery do not replace either native handle");
  SPI.reset();
  corruptRegister = 0x80;
  fixture.releaseForStop();
  runner.expectTrue(display.recover() == hal::Display::InitResult::HardwareError && !owner.available(),
                    "failed volatile-register restoration leaves touch and display unavailable");
  fixture.stopComplete();
  runner.expectTrue(SPI.writeCount == 0 && SPI.transferCount == 0,
                    "failed touch restoration sends no display initialization commands");
  corruptRegister = -1;
  runner.expectTrue(display.recover() == hal::Display::InitResult::Ok,
                    "later display recovery retries the volatile touch configuration");
  fixture.waitPaused();
  testResetGpioEvents();
  SPI.reset();
  testEventWaitFailure = true;
  runner.expectTrue(display.recover() == hal::Display::InitResult::HardwareError,
                    "failed cooperative stop prevents a shared reset");
  runner.expectTrue(!wrote(7, LOW) && SPI.writeCount == 0 && SPI.transferCount == 0,
                    "failed reset ownership sends no controller commands or reset pulse");
  runner.expectTrue(std::none_of(testGpioEvents, testGpioEvents + testGpioEventCount,
                                 [](const TestGpioEvent& event) { return event.pin == 7; }),
                    "failed cooperative stop cannot change reset direction or release its hold");
  const auto restarts = ESP.restartCount;
  input.shutdown();
  runner.expectTrue(ESP.restartCount == restarts + 1 && !wrote(45, HIGH),
                    "failed input shutdown restarts without powering down a live acquisition owner");
  testEventWaitFailure = false;
  fixture.releaseForStop();
  input.shutdown();
  fixture.stopComplete();
  runner.expectFalse(owner.available(), "shutdown removes input availability");
  runner.expectTrue(owner.shutdown(), "shutdown is idempotent");
  runner.expectFalse(owner.poll().fresh, "stopped owner cannot emit retained samples");
  runner.expectEq(0, getForceDeleteCount().load(), "touch tasks stop themselves");
  testSetDigitalReadHook(nullptr);
  testSetDigitalWriteHook(nullptr);
  std::free(display.getFrameBuffer());
  return runner.allPassed() ? 0 : 1;
}
