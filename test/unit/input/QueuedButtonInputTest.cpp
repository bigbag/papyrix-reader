#include <InputManager.h>
#include <MappedInputManager.h>
#include <TargetConfig.h>
#include <freertos/task.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "hal/Input.h"
#include "test_utils.h"

InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);

namespace {
std::atomic<bool> backHeld{false};
std::mutex sampleMutex;
std::condition_variable sampled;
unsigned samples = 0;

bool holdBack(bool held) {
  std::unique_lock<std::mutex> lock(sampleMutex);
  backHeld.store(held);
  samples = 0;
  return sampled.wait_for(lock, std::chrono::seconds(2), [] { return samples >= 8; });
}
}

int main() {
  using namespace papyrix;
  TestUtils::TestRunner runner("Buttons during blocked rendering");
  testSetDigitalReadHook([](int pin) {
    if (pin == 3) {
      std::lock_guard<std::mutex> lock(sampleMutex);
      ++samples;
      sampled.notify_all();
    }
    return pin == 9 && backHeld.load() ? LOW : HIGH;
  });
  testSetAnalogReadHook([](int pin) { return pin == 1 && backHeld.load() ? 3512 : 4095; });
  inputManager.begin();
  EventQueue queue;
  hal::Input input;
  runner.expectTrue(input.init(queue).ok(), "input initializes");

  const bool firstPress = holdBack(true);
  if (firstPress) {
    inputManager.update();
    input.poll();
    Event event{};
    runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonPress && event.button == Button::Back,
                      "first Back press starts the screen transition");
  }
  const bool captured = firstPress && holdBack(false) && holdBack(true) && holdBack(false);
  runner.expectTrue(captured, "hardware sampling continues while rendering blocks the main loop");
  if (captured) {
    vTaskDelay(pdMS_TO_TICKS(hal::Input::LONG_PRESS_MS + 100));
    for (const auto type : {EventType::ButtonRelease, EventType::ButtonPress, EventType::ButtonRelease}) {
      inputManager.update();
      input.poll();
      Event event{};
      runner.expectTrue(queue.pop(event) && event.type == type && event.button == Button::Back,
                        "two Back taps retain both press and release edges in order");
    }
    Event event{};
    runner.expectFalse(queue.pop(event), "short taps do not become a long press or repeat");
    runner.expectFalse(input.isPressed(Button::Back), "Back is released after replay");

    bool overflowCaptured = true;
    for (int i = 0; i < 20 && overflowCaptured; ++i) {
      overflowCaptured = holdBack(true) && holdBack(false);
    }
    runner.expectTrue(overflowCaptured, "sampling continues through an extended render stall");
    bool onlyEdges = true;
    for (int i = 0; i < 64; ++i) {
      inputManager.update();
      input.poll();
      while (queue.pop(event)) {
        onlyEdges &= event.type == EventType::ButtonPress || event.type == EventType::ButtonRelease;
      }
    }
    runner.expectTrue(onlyEdges && !input.isPressed(Button::Back),
                      "overflow retains the final release without a false hold action");
  }

  input.shutdown();
  {
    std::unique_lock<std::mutex> lock(sampleMutex);
    const unsigned stoppedAt = samples;
    backHeld.store(true);
    runner.expectFalse(sampled.wait_for(lock, std::chrono::milliseconds(30), [&] { return samples != stoppedAt; }),
                       "shutdown stops hardware access before sleep");
  }
  runner.expectTrue(input.init(queue).ok(), "input sampling restarts after shutdown");
  runner.expectTrue(holdBack(true), "restarted sampler observes a physical press");
  inputManager.update();
  input.poll();
  Event restarted{};
  runner.expectTrue(queue.pop(restarted) && restarted.type == EventType::ButtonPress &&
                        restarted.button == Button::Back,
                    "restart does not replay stale transitions");
  runner.expectTrue(holdBack(false), "restart hold clears through the sampler");
  {
    Event event{};
    for (int i = 0; i < 8; ++i) {
      inputManager.update();
      input.poll();
      while (queue.pop(event)) {
      }
    }
  }
  runner.expectFalse(input.isPressed(Button::Back), "restart hold is released before the tap test");
  runner.expectTrue(holdBack(true), "sampler observes the tap press");
  runner.expectTrue(holdBack(false), "sampler observes the tap release");
  {
    // The press below spans several samples, so it survives the slow idle rate.
    // This is an acknowledged tradeoff, not full coverage: a press contained
    // entirely between two 50 ms idle samples is still lost.
    std::vector<Event> tap;
    Event event{};
    for (int i = 0; i < 8; ++i) {
      inputManager.update();
      input.poll();
      while (queue.pop(event)) tap.push_back(event);
    }
    const bool pressThenRelease =
        tap.size() == 2 && tap[0].type == EventType::ButtonPress && tap[0].button == Button::Back &&
        tap[1].type == EventType::ButtonRelease && tap[1].button == Button::Back;
    runner.expectTrue(pressThenRelease, "idle-rate sampling keeps a normal tap press and release in order");
  }

  input.shutdown();
  testSetDigitalReadHook(nullptr);
  testSetAnalogReadHook(nullptr);
  cleanupMockTasks();
  return runner.allPassed() ? 0 : 1;
}
