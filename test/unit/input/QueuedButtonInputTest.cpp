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

// Lock-step scheduler for the idle-gap tap sweep. The delay hook runs on the
// sampling task; every other member runs on the main thread.
std::mutex stepMutex;
std::condition_variable stepCv;
bool stepEnterRequested = false;     // guarded by stepMutex
bool stepActive = false;             // guarded by stepMutex
bool stepParked = false;             // guarded by stepMutex
bool stepRelease = false;            // guarded by stepMutex
bool stepBroken = false;             // guarded by stepMutex
unsigned long tapWindowStartMs = 0;  // written only while the task is parked
unsigned long tapWindowEndMs = 0;

bool backHeldNow() {
  const unsigned long now = millis();
  return backHeld.load() || (now >= tapWindowStartMs && now < tapWindowEndMs);
}

void sweepDelayHook(const TickType_t ticks) {
  std::unique_lock<std::mutex> lock(stepMutex);
  if (!stepEnterRequested) {
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
    return;
  }
  if (stepActive) testManualMillisValue += ticks;  // pdMS_TO_TICKS(ms) == ms in this mock
  stepParked = true;
  stepCv.notify_all();
  stepCv.wait(lock, [] { return stepRelease || !stepEnterRequested; });
  stepRelease = false;
  stepParked = false;
}

// Parks the sampling task once so the main thread can switch to manual time.
bool enterLockstep() {
  std::unique_lock<std::mutex> lock(stepMutex);
  stepEnterRequested = true;
  stepCv.notify_all();
  if (!stepCv.wait_for(lock, std::chrono::seconds(2), [] { return stepParked; })) {
    stepBroken = true;
    return false;
  }
  // The task is parked inside vTaskDelay: switch clocks without a race.
  testSetManualMillis(1000000);
  stepActive = true;
  stepParked = false;
  stepRelease = true;
  stepCv.notify_all();
  if (!stepCv.wait_for(lock, std::chrono::seconds(2), [] { return stepParked; })) {
    stepBroken = true;
    return false;
  }
  return true;
}

void exitLockstep() {
  std::unique_lock<std::mutex> lock(stepMutex);
  testUseRealtimeMillis();
  tapWindowStartMs = tapWindowEndMs = 0;
  stepActive = false;
  stepEnterRequested = false;
  stepRelease = true;
  stepCv.notify_all();
}

// Releases the parked task for one production loop iteration: one sample at
// the parked time plus one delay. Returns the new parked time, which is also
// the time the next sample reads.
unsigned long stepSampler() {
  std::unique_lock<std::mutex> lock(stepMutex);
  if (stepBroken || !stepParked) {
    stepBroken = true;
    return testManualMillisValue;
  }
  stepParked = false;
  stepRelease = true;
  stepCv.notify_all();
  if (!stepCv.wait_for(lock, std::chrono::seconds(2), [] { return stepParked; })) stepBroken = true;
  return testManualMillisValue;
}

// Runs one tap at the given onset phase through the real sampling task and
// drains it. The task is parked on entry and on exit. Returns the new parked
// time; ok reports whether the observed events match the expectation.
unsigned long runTapPhase(papyrix::hal::Input& input, papyrix::EventQueue& queue, const unsigned long baseMs,
                          const unsigned long durationMs, const unsigned long phaseMs, const bool expectTap, bool& ok) {
  tapWindowStartMs = baseMs + phaseMs;
  tapWindowEndMs = tapWindowStartMs + durationMs;
  const unsigned long deadlineMs = tapWindowEndMs + 120;  // release observe plus debounce and active ticks
  unsigned long nowMs = baseMs;
  for (int guard = 0; guard < 2000 && !stepBroken && nowMs < deadlineMs; ++guard) nowMs = stepSampler();
  if (stepBroken) {
    ok = false;
    return nowMs;
  }
  for (int i = 0; i < 4; ++i) {
    inputManager.update();
    input.poll();
  }
  std::vector<papyrix::Event> events;
  papyrix::Event event{};
  while (queue.pop(event)) events.push_back(event);
  const bool pressThenRelease = events.size() == 2 && events[0].type == papyrix::EventType::ButtonPress &&
                                events[0].button == papyrix::Button::Back &&
                                events[1].type == papyrix::EventType::ButtonRelease &&
                                events[1].button == papyrix::Button::Back;
  const bool silence = events.empty();
  ok = (expectTap ? pressThenRelease : silence) && !input.isPressed(papyrix::Button::Back);
  return nowMs;
}
}  // namespace

int main() {
  using namespace papyrix;
  TestUtils::TestRunner runner("Buttons during blocked rendering");
  testSetDigitalReadHook([](int pin) {
    if (pin == 3) {
      std::lock_guard<std::mutex> lock(sampleMutex);
      ++samples;
      sampled.notify_all();
    }
    return pin == 9 && backHeldNow() ? LOW : HIGH;
  });
  testSetAnalogReadHook([](int pin) { return pin == 1 && backHeldNow() ? 3512 : 4095; });
  testSetVTaskDelayHook(sweepDelayHook);
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
  runner.expectTrue(
      queue.pop(restarted) && restarted.type == EventType::ButtonPress && restarted.button == Button::Back,
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
    // The press below spans several samples; the lock-step sweep below also
    // covers taps that start between two idle samples.
    std::vector<Event> tap;
    Event event{};
    for (int i = 0; i < 8; ++i) {
      inputManager.update();
      input.poll();
      while (queue.pop(event)) tap.push_back(event);
    }
    const bool pressThenRelease = tap.size() == 2 && tap[0].type == EventType::ButtonPress &&
                                  tap[0].button == Button::Back && tap[1].type == EventType::ButtonRelease &&
                                  tap[1].button == Button::Back;
    runner.expectTrue(pressThenRelease, "idle-rate sampling keeps a normal tap press and release in order");
  }

  // Deterministic idle-gap sweep: the real sampling task runs one production
  // loop per step on a manual clock, so tap onset phases are exact.
  if (enterLockstep()) {
    const unsigned long tickA = stepSampler();
    const unsigned long tickB = stepSampler();
    const unsigned long idleIntervalMs = tickB - tickA;
    if (runner.expectTrue(idleIntervalMs >= 1 && idleIntervalMs <= 100, "sampler reports a bounded idle interval")) {
      unsigned long baseMs = tickB;
      bool tapsOk = true;
      for (const unsigned long durationMs : {50UL, 65UL, 75UL}) {
        for (unsigned long phaseMs = 0; phaseMs <= idleIntervalMs && !stepBroken; ++phaseMs) {
          bool phaseOk = false;
          baseMs = runTapPhase(input, queue, baseMs, durationMs, phaseMs, true, phaseOk);
          tapsOk &= runner.expectTrue(
              phaseOk, "tap " + std::to_string(durationMs) + " ms at onset phase " + std::to_string(phaseMs) + " ms",
              "", true);
        }
      }
      runner.expectTrue(tapsOk, "50/65/75 ms taps register at every idle onset phase");
      bool pulsesOk = true;
      for (const unsigned long durationMs : {10UL, 20UL}) {
        for (unsigned long phaseMs = 0; phaseMs <= idleIntervalMs && !stepBroken; ++phaseMs) {
          bool phaseOk = false;
          baseMs = runTapPhase(input, queue, baseMs, durationMs, phaseMs, false, phaseOk);
          pulsesOk &= runner.expectTrue(
              phaseOk, "pulse " + std::to_string(durationMs) + " ms at onset phase " + std::to_string(phaseMs) + " ms",
              "", true);
        }
      }
      runner.expectTrue(pulsesOk, "pulses within the 20 ms debounce window are dropped");
    }
  } else {
    runner.expectTrue(false, "sampler enters lock-step for the idle-gap sweep");
  }
  exitLockstep();

  input.shutdown();
  testSetDigitalReadHook(nullptr);
  testSetAnalogReadHook(nullptr);
  testSetVTaskDelayHook(nullptr);
  cleanupMockTasks();
  return runner.allPassed() ? 0 : 1;
}
