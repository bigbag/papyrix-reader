#include <InputManager.h>
#include <MappedInputManager.h>
#include <freertos/task.h>

#include <atomic>
#include <thread>

#include "hal/Input.h"
#include "test_utils.h"

InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);

int main() {
  using namespace papyrix;
  TestUtils::TestRunner runner("HAL button release during refresh");
  static int pressedPin = -1;
  testSetDigitalReadHook([](int pin) { return pin == pressedPin ? LOW : HIGH; });
  testSetManualMillis(100);
  inputManager.begin();
  EventQueue queue;
  hal::Input input;
  enableTaskCreateFailureGate();
  std::atomic<bool> initialized{false};
  std::thread initThread([&] { initialized = input.init(queue).ok(); });
  waitForTaskCreateBlocked();
  releaseTaskCreateFailureGate();
  initThread.join();
  runner.expectTrue(initialized.load(), "input initializes when sampler task creation fails");
  inputManager.stopSampling();
  const auto sample = [&](unsigned long time) {
    testManualMillisValue = time;
    inputManager.update();
    input.poll();
  };
  Event event{};
  sample(100);
  pressedPin = 7;
  sample(110);
  runner.expectFalse(queue.pop(event), "press still requires debounce");
  sample(140);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonPress && event.button == Button::Up,
                    "stable press moves up once");
  sample(840);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonRepeat && event.button == Button::Up,
                    "held direction still repeats");
  pressedPin = -1;
  sample(1240);
  runner.expectFalse(queue.pop(event), "release during refresh cannot emit a trailing repeat");
  sample(1265);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonRelease && event.button == Button::Up,
                    "release completes after debounce");

  pressedPin = 0;
  sample(1270);
  sample(1295);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonPress && event.button == Button::Down,
                    "opposite side button moves down");
  pressedPin = -1;
  sample(1320);
  sample(1345);
  queue.pop(event);

  pressedPin = 3;
  sample(1370);
  sample(1395);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonPress && event.button == Button::Power,
                    "power press is delivered");
  pressedPin = -1;
  sample(3330);
  runner.expectFalse(queue.pop(event), "release during refresh cannot become a long press");
  sample(3355);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonRelease && event.button == Button::Power,
                    "power release remains debounced");
  pressedPin = 3;
  sample(3400);
  sample(3430);
  queue.pop(event);
  sample(5430);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonLongPress && event.button == Button::Power,
                    "a physical hold still emits long press");
  pressedPin = -1;
  sample(5455);
  sample(5480);
  queue.pop(event);  // power release from the case above

  // Suppression models setup: the button is debounce-stable held before the
  // latch arms, and the hold past 700 ms must not emit a long press.
  pressedPin = 3;
  sample(5485);
  sample(5515);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonPress && event.button == Button::Power,
                    "press before suppression is delivered");
  input.suppressPowerUntilRelease();
  runner.expectTrue(input.powerSuppressed(), "suppression armed while held");
  sample(6250);
  runner.expectTrue(input.powerSuppressed(), "suppression survives the hold");
  runner.expectFalse(queue.pop(event), "suppressed hold emits no long press");
  pressedPin = -1;
  sample(6275);
  sample(6300);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonRelease && event.button == Button::Power,
                    "suppressed hold releases normally");
  runner.expectFalse(input.powerSuppressed(), "release clears suppression");
  pressedPin = 3;
  sample(6400);
  sample(6430);
  queue.pop(event);  // press
  sample(7150);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonLongPress && event.button == Button::Power,
                    "press after suppression emits long press again");
  pressedPin = -1;
  sample(7175);
  sample(7200);
  queue.pop(event);  // release

  // A release before the first poll must clear the latch without an edge.
  input.suppressPowerUntilRelease();
  pressedPin = 3;
  // No polls while the button is down.
  pressedPin = -1;
  sample(7300);
  runner.expectFalse(input.powerSuppressed(), "release before first poll clears suppression");
  runner.expectFalse(queue.pop(event), "no events from an unobserved press");
  pressedPin = 3;
  sample(7400);
  sample(7430);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonPress && event.button == Button::Power,
                    "press after edgeless clear is delivered");
  sample(8150);
  runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonLongPress && event.button == Button::Power,
                    "hold after edgeless clear emits long press");
  input.shutdown();
  cleanupMockTasks();
  testSetDigitalReadHook(nullptr);
  testManualMillisEnabled = false;
  return runner.allPassed() ? 0 : 1;
}
