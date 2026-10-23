#include <InputManager.h>
#include <MappedInputManager.h>
#include <Wire.h>

#include "hal/Input.h"
#include "test_utils.h"

InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);

int main() {
  TestUtils::TestRunner runner("GT911 HAL input");
  using namespace papyrix;
  Wire.reset();
  Wire.setPresent(0x5D, true);
  Wire.setWideAddress(0x5D);
  testSetManualMillis(100);
  EventQueue queue;
  hal::Input input;
  runner.expectTrue(input.init(queue).ok(), "touch input initializes");
  inputManager.stopSampling();
  const auto poll = [&](uint8_t status) {
    Wire.setRegister(0x5D, 0x814E, status);
    testManualMillisValue += 50;
    input.poll();
  };
  Event event{};
  poll(0);
  poll(0x90);
  poll(0);
  runner.expectFalse(queue.pop(event), "held Home does not emit an early Back or Tap");
  poll(0x80);
#if PAPYRIX_TARGET_STICKY
  runner.expectFalse(queue.pop(event), "Sticky has no capacitive Home key");
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
  runner.expectFalse(queue.pop(event), "release emits a single Tap");

  poll(0x81);
  runner.expectFalse(queue.pop(event), "second contact holds events until release");
  board::suppressTouchUntilIdle();
  poll(0);
  poll(0x81);
  poll(0x80);
  runner.expectFalse(queue.pop(event), "refresh during a held contact cannot create a tap");
  input.shutdown();
  cleanupMockTasks();
  testManualMillisEnabled = false;
  return runner.allPassed() ? 0 : 1;
}
