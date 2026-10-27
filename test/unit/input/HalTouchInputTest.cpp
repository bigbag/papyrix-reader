#include <InputManager.h>
#include <MappedInputManager.h>
#include <Wire.h>

#if PAPYRIX_TARGET_MURPHY_M4
#include "MurphyM4TouchFixture.h"
#endif
#include "hal/Input.h"
#include "test_utils.h"

InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);

int main() {
  TestUtils::TestRunner runner("GT911 HAL input");
  using namespace papyrix;
#if PAPYRIX_TARGET_MURPHY_M4
  MurphyM4TouchFixture fixture;
#endif
  Wire.reset();
  Wire.setPresent(0x5D, true);
  Wire.setWideAddress(0x5D);
  testSetManualMillis(100);
  EventQueue queue;
  hal::Input input;
  runner.expectTrue(input.init(queue).ok(), "touch input initializes");
  inputManager.stopSampling();
#if PAPYRIX_TARGET_MURPHY_M4
  fixture.waitPaused();
  auto frame = MurphyM4TouchFixture::frame(0);
#endif
  const auto poll = [&](uint8_t status) {
#if PAPYRIX_TARGET_MURPHY_M4
    if (status != 0) frame = MurphyM4TouchFixture::frame(status & 0x0F ? 1 : 0);
    fixture.step(static_cast<uint32_t>(millis()) + 50, frame);
#else
    Wire.setRegister(0x5D, 0x814E, status);
    testManualMillisValue += 50;
#endif
    input.poll();
  };
  Event event{};
  poll(0);
  poll(0x90);
  poll(0);
  runner.expectFalse(queue.pop(event), "held Home does not emit an early Back or Tap");
  poll(0x80);
#if PAPYRIX_TARGET_STICKY || PAPYRIX_TARGET_MURPHY_M4
  runner.expectFalse(queue.pop(event), "board has no capacitive Home key");
#else
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonPress && event.button == Button::Back,
                    "first Home release after an unchanged idle poll emits Back");
#endif
  poll(0);
  runner.expectFalse(queue.pop(event), "acknowledged Home release does not repeat");

  board::suppressTouchUntilIdle();
  poll(0);
  Wire.setRegister(0x5D, 0x8150, 100);
  Wire.setRegister(0x5D, 0x8152, 200);
  poll(0x81);
  runner.expectFalse(queue.pop(event), "first contact after a refresh holds events until release");
  poll(0);
  runner.expectFalse(queue.pop(event), "unchanged held contact emits nothing");
  poll(0x80);
  runner.expectTrue(queue.pop(event) && event.type == EventType::Tap, "first post-refresh contact emits Tap");
  runner.expectEq(uint32_t{0}, input.userIdleTimeMs(), "an accepted tap records activity without consuming its action");
  runner.expectFalse(queue.pop(event), "release emits a single Tap");
  const auto lastTapMs = static_cast<uint32_t>(millis());
  testManualMillisValue += 123;
  input.resetIdleTimer();
  runner.expectEq(uint32_t{0}, input.idleTimeMs(), "network keep-awake resets only the sleep timer");
  runner.expectEq(uint32_t{123}, input.userIdleTimeMs(), "touch inactivity survives network keep-awake");

  poll(0x81);
  runner.expectFalse(queue.pop(event), "second contact holds events until release");
  board::suppressTouchUntilIdle();
  poll(0);
  poll(0x81);
  poll(0x80);
  runner.expectFalse(queue.pop(event), "refresh during a held contact cannot create a tap");
  runner.expectEq(static_cast<uint32_t>(millis()) - lastTapMs, input.userIdleTimeMs(),
                  "a suppressed contact does not restore the light");
#if PAPYRIX_TARGET_MURPHY_M4
  fixture.releaseForStop();
#endif
  input.shutdown();
  cleanupMockTasks();
  testManualMillisEnabled = false;
  return runner.allPassed() ? 0 : 1;
}
