#include <HardwareIdentity.h>
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
#include "core/PapyrixSettings.h"
#include "core/ReaderButtonDispatcher.h"
#include "test_utils.h"

InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);

namespace {
constexpr papyrix::Button kTestButton = PAPYRIX_TARGET_STICKY ? papyrix::Button::Up : papyrix::Button::Back;
std::atomic<bool> backHeld{false};
std::atomic<uint8_t> waitHeld{0};
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
                                events[0].button == kTestButton &&
                                events[1].type == papyrix::EventType::ButtonRelease &&
                                events[1].button == kTestButton;
  const bool silence = events.empty();
  ok = (expectTap ? pressThenRelease : silence) && !input.isPressed(kTestButton);
  return nowMs;
}
}  // namespace

int main() {
  using namespace papyrix;
  TestUtils::TestRunner runner("Buttons during blocked rendering");
  testSetDigitalReadHook([](int pin) {
    if (pin == (PAPYRIX_TARGET_STICKY ? 4 : 3)) {
      std::lock_guard<std::mutex> lock(sampleMutex);
      ++samples;
      sampled.notify_all();
    }
    const auto& pins = papyrix::board::HardwareIdentity::instance().profile().input;
    const int physicalPins[] = {pins.back, pins.confirm, pins.left, pins.right, pins.up, pins.down, pins.power};
    for (unsigned button = 0; button < 7; ++button) {
      if (pin == physicalPins[button] && (waitHeld.load() & (1u << button))) return LOW;
    }
    return pin == (PAPYRIX_TARGET_STICKY ? 5 : 9) && backHeldNow() ? LOW : HIGH;
  });
  testSetAnalogReadHook([](int pin) {
    if (pin == 1 && backHeldNow()) return 3512;
    const uint8_t held = waitHeld.load();
    const int values[] = {3512, 2694, 1493, 5, 2242, 5};
    for (unsigned button = pin == 1 ? 0 : 4; button < (pin == 1 ? 4u : 6u); ++button) {
      if (held & (1u << button)) return values[button];
    }
    return 4095;
  });
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
    runner.expectTrue(queue.pop(event) && event.type == EventType::ButtonPress && event.button == kTestButton,
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
      runner.expectTrue(queue.pop(event) && event.type == type && event.button == kTestButton,
                        "two Back taps retain both press and release edges in order");
    }
    Event event{};
    runner.expectFalse(queue.pop(event), "short taps do not become a long press or repeat");
    runner.expectFalse(input.isPressed(kTestButton), "sampled button is released after replay");

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
    runner.expectTrue(onlyEdges && !input.isPressed(kTestButton),
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
      queue.pop(restarted) && restarted.type == EventType::ButtonPress && restarted.button == kTestButton,
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
  runner.expectFalse(input.isPressed(kTestButton), "restart hold is released before the tap test");
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
                                  tap[0].button == kTestButton && tap[1].type == EventType::ButtonRelease &&
                                  tap[1].button == kTestButton;
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
    tapWindowStartMs = tapWindowEndMs = 0;
    const auto settle = [&](uint8_t held) {
      waitHeld.store(held);
      for (unsigned tick = 0; tick < 8 && !stepBroken; ++tick) stepSampler();
    };
    ReaderButtonDispatcher dispatch;
    const ReaderButtonConfig dispatchConfig{};
    std::vector<ReaderButtonAction> actions;
    std::vector<Button> buttons;
    std::vector<EventType> edges;
    const auto drain = [&]() {
      for (unsigned tick = 0; tick < 40; ++tick) {
        inputManager.update();
        input.poll();
        Event event{};
        while (queue.pop(event)) {
          const auto action = dispatch.processEvent(event, millis(), dispatchConfig);
          if (action == ReaderButtonAction::Next || action == ReaderButtonAction::Prev ||
              action == ReaderButtonAction::NextChapter || action == ReaderButtonAction::PrevChapter) {
            actions.push_back(action);
          }
          if ((event.type == EventType::ButtonPress || event.type == EventType::ButtonRelease) &&
              (event.button == Button::Back || event.button == Button::Power || event.button == Button::Center)) {
            buttons.push_back(event.button);
            edges.push_back(event.type);
          }
        }
      }
    };
    constexpr uint8_t next = 1u << InputManager::BTN_DOWN;
    constexpr uint8_t left = 1u << (PAPYRIX_TARGET_STICKY ? InputManager::BTN_UP : InputManager::BTN_LEFT);
    settle(next);
    settle(0);
    input.setPageButtonsBlocked(true);
    settle(next);
    settle(0);
    input.setPageButtonsBlocked(false);
    drain();
    runner.expectTrue(actions == std::vector<ReaderButtonAction>{ReaderButtonAction::Next},
                      "complete pre-wait tap survives without replaying wait taps");

    actions.clear();
    settle(next);
    drain();
    input.setPageButtonsBlocked(true);
    settle(next);
    input.setPageButtonsBlocked(false);
    for (unsigned tick = 0; tick < 160; ++tick) stepSampler();
    drain();
    settle(0);
    drain();
    runner.expectTrue(actions.empty(), "held page button cannot repeat or navigate on its release");
    settle(next);
    settle(0);
    drain();
    runner.expectTrue(actions == std::vector<ReaderButtonAction>{ReaderButtonAction::Next},
                      "fresh tap after a held release navigates once");

    actions.clear();
    input.setPageButtonsBlocked(true);
    waitHeld.store(next);
    input.setPageButtonsBlocked(false);
    settle(next);
    drain();
    settle(0);
    drain();
    runner.expectTrue(actions.empty(), "unsampled press at completion still requires release");
    settle(next);
    settle(0);
    drain();
    runner.expectTrue(actions == std::vector<ReaderButtonAction>{ReaderButtonAction::Next},
                      "fresh tap works after the late press releases");

    actions.clear();
    input.setPageButtonsBlocked(true);
    settle(next | left);
    input.setPageButtonsBlocked(false);
    drain();
    settle(left);
    drain();
    settle(next | left);
    settle(left);
    drain();
    runner.expectTrue(actions == std::vector<ReaderButtonAction>{ReaderButtonAction::Next},
                      "released page button can navigate while another button remains suppressed");
    settle(0);
    drain();
    settle(left);
    settle(0);
    drain();
    runner.expectTrue(actions == std::vector<ReaderButtonAction>{ReaderButtonAction::Next, ReaderButtonAction::Prev},
                      "each held button requires its own release and fresh press");

    for (unsigned tap = 0; tap < 20; ++tap) {
      settle(next);
      settle(0);
      drain();
    }
    actions.clear();
    buttons.clear();
    edges.clear();
    input.setPageButtonsBlocked(true);
    for (unsigned tap = 0; tap < 40; ++tap) {
      settle(next);
      settle(0);
      if ((!PAPYRIX_TARGET_STICKY && tap == 7) || tap == 20) {
        settle(1u << (tap == 7 ? InputManager::BTN_BACK : InputManager::BTN_POWER));
        settle(0);
      }
    }
    input.setPageButtonsBlocked(false);
    drain();
    runner.expectTrue(actions.empty(), "blocked page taps cannot overflow into navigation");
    const std::vector<Button> retainedButtons = PAPYRIX_TARGET_STICKY
                                                   ? std::vector<Button>{Button::Power, Button::Power}
                                                   : std::vector<Button>{Button::Back, Button::Back, Button::Power,
                                                                         Button::Power};
    const std::vector<EventType> retainedEdges =
        PAPYRIX_TARGET_STICKY
            ? std::vector<EventType>{EventType::ButtonPress, EventType::ButtonRelease}
            : std::vector<EventType>{EventType::ButtonPress, EventType::ButtonRelease, EventType::ButtonPress,
                                     EventType::ButtonRelease};
    runner.expectTrue(buttons == retainedButtons && edges == retainedEdges,
                      "wrapped queue keeps available Back and Power edges during blocked taps");

    actions.clear();
    buttons.clear();
    edges.clear();
    constexpr uint8_t firstHeld = 1u << (PAPYRIX_TARGET_STICKY ? InputManager::BTN_POWER : InputManager::BTN_BACK);
    constexpr Button firstButton = PAPYRIX_TARGET_STICKY ? Button::Power : Button::Back;
    settle(firstHeld);
    settle(0);
    for (unsigned tap = 0; tap < 15; ++tap) {
      settle(next);
      settle(0);
    }
    input.setPageButtonsBlocked(true);
    settle(next);
    settle(0);
    input.setPageButtonsBlocked(false);
    drain();
    runner.expectTrue(buttons == std::vector<Button>{firstButton, firstButton} &&
                          edges == std::vector<EventType>{EventType::ButtonPress, EventType::ButtonRelease},
                      "suppression boundary preserves the first non-page tap in a full queue");
    runner.expectTrue(actions == std::vector<ReaderButtonAction>(15, ReaderButtonAction::Next),
                      "full pre-wait queue keeps every completed page tap");

    Settings settings{};
    mappedInputManager.setSettings(&settings);
#if !PAPYRIX_TARGET_STICKY
    for (bool reversed : {false, true}) {
      settings.frontButtonLayout = reversed ? Settings::FrontLRBC : Settings::FrontBCLR;
      settings.sideButtonLayout = reversed ? Settings::NextPrev : Settings::PrevNext;
      const uint8_t frontNext = 1u << (reversed ? InputManager::BTN_CONFIRM : InputManager::BTN_RIGHT);
      const uint8_t back = 1u << (reversed ? InputManager::BTN_LEFT : InputManager::BTN_BACK);
      const uint8_t confirm = 1u << (reversed ? InputManager::BTN_RIGHT : InputManager::BTN_CONFIRM);
      const uint8_t sideNext = 1u << (reversed ? InputManager::BTN_UP : InputManager::BTN_DOWN);
      actions.clear();
      buttons.clear();
      input.setPageButtonsBlocked(true);
      for (uint8_t key : {frontNext, sideNext, back, confirm}) {
        settle(key);
        settle(0);
      }
      input.setPageButtonsBlocked(false);
      drain();
      runner.expectTrue(actions.empty() &&
                            buttons == std::vector<Button>{Button::Back, Button::Back, Button::Center, Button::Center},
                        "configured page mapping keeps Back and Confirm available");
      settle(frontNext);
      settle(0);
      settle(sideNext);
      settle(0);
      drain();
      runner.expectTrue(actions == std::vector<ReaderButtonAction>{ReaderButtonAction::Next, ReaderButtonAction::Next},
                        "fresh front and side taps follow the configured layouts");
    }
#endif
    mappedInputManager.setSettings(nullptr);
    runner.expectFalse(stepBroken, "cache-wait sampling remains deterministic");
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
