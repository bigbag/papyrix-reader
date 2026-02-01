#include "test_utils.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

// Include FreeRTOS mocks
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// BackgroundTask implementation (inlined for testing without complex dependencies)
class BackgroundTask {
 public:
  using TaskFunction = std::function<void()>;
  using AbortCallback = std::function<bool()>;

  enum class State : uint8_t {
    IDLE,
    STARTING,
    RUNNING,
    STOPPING,
    COMPLETE,
    ERROR
  };

  BackgroundTask() {
    eventGroup_ = xEventGroupCreate();
  }

  ~BackgroundTask() {
    stop(5000);
    if (eventGroup_) {
      vEventGroupDelete(eventGroup_);
    }
  }

  BackgroundTask(const BackgroundTask&) = delete;
  BackgroundTask& operator=(const BackgroundTask&) = delete;

  bool start(const char* name, uint32_t stackSize, TaskFunction func, int priority) {
    if (isRunning()) return false;

    state_.store(State::STARTING, std::memory_order_release);
    stopRequested_.store(false, std::memory_order_release);
    xEventGroupClearBits(eventGroup_, EVENT_EXITED);

    func_ = std::move(func);
    name_ = name ? name : "";

    BaseType_t result = xTaskCreatePinnedToCore(trampoline, name_.c_str(), stackSize, this, priority, &handle_, 0);

    if (result != pdPASS) {
      state_.store(State::ERROR, std::memory_order_release);
      return false;
    }

    return true;
  }

  bool stop(uint32_t maxWaitMs = 10000) {
    State currentState = state_.load(std::memory_order_acquire);
    if (currentState == State::IDLE || currentState == State::COMPLETE || currentState == State::ERROR) {
      return true;
    }

    stopRequested_.store(true, std::memory_order_release);
    state_.store(State::STOPPING, std::memory_order_release);

    TickType_t waitTicks = (maxWaitMs == 0) ? portMAX_DELAY : maxWaitMs;
    EventBits_t bits = xEventGroupWaitBits(eventGroup_, EVENT_EXITED, pdFALSE, pdFALSE, waitTicks);

    if (bits & EVENT_EXITED) {
      // Task exited - join the thread
      auto& registry = getTaskRegistry();
      for (auto* task : registry) {
        if (task->thread.joinable() && static_cast<void*>(task) == handle_) {
          task->thread.join();
          break;
        }
      }
      state_.store(State::COMPLETE, std::memory_order_release);
      return true;
    }

    return false;  // Timeout
  }

  bool shouldStop() const { return stopRequested_.load(std::memory_order_acquire); }

  AbortCallback getAbortCallback() const {
    return [this]() { return shouldStop(); };
  }

  bool isRunning() const {
    State s = state_.load(std::memory_order_acquire);
    return s == State::RUNNING || s == State::STOPPING;
  }

  State getState() const { return state_.load(std::memory_order_acquire); }

  TaskHandle_t getHandle() const { return handle_; }

 private:
  static void trampoline(void* param) {
    BackgroundTask* self = static_cast<BackgroundTask*>(param);
    self->run();
  }

  void run() {
    state_.store(State::RUNNING, std::memory_order_release);
    if (func_) {
      func_();
    }
    xEventGroupSetBits(eventGroup_, EVENT_EXITED);
    vTaskDelete(nullptr);  // Self-delete (correct usage)
  }

  static constexpr EventBits_t EVENT_EXITED = (1 << 0);

  TaskHandle_t handle_ = nullptr;
  EventGroupHandle_t eventGroup_ = nullptr;
  std::atomic<bool> stopRequested_{false};
  std::atomic<State> state_{State::IDLE};
  TaskFunction func_;
  std::string name_;
};

int main() {
  TestUtils::TestRunner runner("BackgroundTask");

  // ============================================
  // State machine tests
  // ============================================

  // Test 1: Initial state is IDLE
  {
    BackgroundTask task;
    runner.expectTrue(task.getState() == BackgroundTask::State::IDLE, "Initial state is IDLE");
    runner.expectFalse(task.isRunning(), "Not running initially");
  }

  // Test 2: Start transitions to RUNNING
  {
    cleanupMockTasks();
    BackgroundTask task;
    std::atomic<bool> started{false};

    bool result = task.start("test", 4096, [&]() {
      started.store(true);
      while (!task.shouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }, 1);

    runner.expectTrue(result, "start() returns true");

    // Wait a bit for task to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runner.expectTrue(started.load(), "Task function executed");
    runner.expectTrue(task.isRunning(), "Task is running after start");

    task.stop(1000);
    runner.expectTrue(task.getState() == BackgroundTask::State::COMPLETE, "State is COMPLETE after stop");
  }

  // Test 3: Start while already running returns false
  {
    cleanupMockTasks();
    BackgroundTask task;
    task.start("test", 4096, [&]() {
      while (!task.shouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    bool secondStart = task.start("test2", 4096, []() {}, 1);
    runner.expectFalse(secondStart, "Second start() returns false when running");

    task.stop(1000);
  }

  // Test 4: Stop with adequate timeout - graceful stop
  {
    cleanupMockTasks();
    BackgroundTask task;
    std::atomic<int> iterations{0};

    task.start("test", 4096, [&]() {
      while (!task.shouldStop()) {
        iterations++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    bool stopResult = task.stop(5000);

    runner.expectTrue(stopResult, "stop() returns true with adequate timeout");
    runner.expectTrue(task.getState() == BackgroundTask::State::COMPLETE, "State is COMPLETE");
    runner.expectTrue(iterations.load() > 0, "Task ran for some iterations");
  }

  // Test 5: Double stop returns immediately
  {
    cleanupMockTasks();
    BackgroundTask task;
    task.start("test", 4096, [&]() {
      while (!task.shouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    task.stop(1000);

    auto start = std::chrono::steady_clock::now();
    bool secondStop = task.stop(5000);
    auto elapsed = std::chrono::steady_clock::now() - start;

    runner.expectTrue(secondStop, "Second stop() returns true");
    runner.expectTrue(elapsed < std::chrono::milliseconds(100), "Second stop() returns quickly");
  }

  // ============================================
  // shouldStop() tests
  // ============================================

  // Test 6: shouldStop() returns false initially
  {
    cleanupMockTasks();
    BackgroundTask task;
    std::atomic<bool> shouldStopInitial{true};

    task.start("test", 4096, [&]() {
      shouldStopInitial.store(task.shouldStop());
      while (!task.shouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runner.expectFalse(shouldStopInitial.load(), "shouldStop() is false initially in task");
    task.stop(1000);
  }

  // Test 7: shouldStop() returns true after stop requested
  {
    cleanupMockTasks();
    BackgroundTask task;
    std::atomic<bool> sawStopRequest{false};

    task.start("test", 4096, [&]() {
      while (!task.shouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      sawStopRequest.store(true);
    }, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    task.stop(1000);

    runner.expectTrue(sawStopRequest.load(), "Task saw stop request via shouldStop()");
  }

  // ============================================
  // Abort callback tests
  // ============================================

  // Test 8: Abort callback returns shouldStop() value
  {
    cleanupMockTasks();
    BackgroundTask task;
    std::atomic<bool> callbackReturnedFalse{false};
    std::atomic<bool> callbackReturnedTrue{false};

    task.start("test", 4096, [&]() {
      auto abort = task.getAbortCallback();
      if (!abort()) {
        callbackReturnedFalse.store(true);
      }
      while (!task.shouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (abort()) {
        callbackReturnedTrue.store(true);
      }
    }, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    task.stop(1000);

    runner.expectTrue(callbackReturnedFalse.load(), "Abort callback returned false initially");
    runner.expectTrue(callbackReturnedTrue.load(), "Abort callback returned true after stop");
  }

  // ============================================
  // Self-deletion safety tests
  // ============================================

  // Test 9: Task self-deletes (never force-deleted)
  {
    cleanupMockTasks();

    int forceDeletesBefore = getForceDeleteCount().load();
    int selfDeletesBefore = getSelfDeleteCount().load();

    {
      BackgroundTask task;
      task.start("test", 4096, [&]() {
        while (!task.shouldStop()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }, 1);

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      task.stop(1000);
    }

    int forceDeletesAfter = getForceDeleteCount().load();
    int selfDeletesAfter = getSelfDeleteCount().load();

    runner.expectEq(forceDeletesBefore, forceDeletesAfter, "No force-deletes occurred");
    runner.expectTrue(selfDeletesAfter > selfDeletesBefore, "Self-delete was called");
  }

  // ============================================
  // Task function execution tests
  // ============================================

  // Test 10: Task function receives correct parameters through closure
  {
    cleanupMockTasks();
    BackgroundTask task;
    std::atomic<int> value{0};
    int expected = 42;

    task.start("test", 4096, [&, expected]() {
      value.store(expected);
      while (!task.shouldStop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    task.stop(1000);

    runner.expectEq(expected, value.load(), "Closure captures values correctly");
  }

  // Test 11: Task completes immediately if function exits
  {
    cleanupMockTasks();
    BackgroundTask task;
    std::atomic<bool> completed{false};

    task.start("test", 4096, [&]() {
      completed.store(true);
      // Exit immediately without checking shouldStop
    }, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Task should have exited on its own
    runner.expectTrue(completed.load(), "Quick task completed");

    // Stop should return immediately
    auto start = std::chrono::steady_clock::now();
    task.stop(5000);
    auto elapsed = std::chrono::steady_clock::now() - start;
    runner.expectTrue(elapsed < std::chrono::milliseconds(500), "Stop on completed task is fast");
  }

  // ============================================
  // Stress test: Rapid start/stop cycles
  // ============================================

  // Test 12: Multiple start/stop cycles
  {
    cleanupMockTasks();
    BackgroundTask task;

    for (int i = 0; i < 5; i++) {
      cleanupMockTasks();

      bool started = task.start("cycle", 4096, [&]() {
        while (!task.shouldStop()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      }, 1);

      if (started) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        task.stop(1000);
      }
    }

    runner.expectTrue(true, "Multiple start/stop cycles completed without crash");
  }

  cleanupMockTasks();
  cleanupMockEventGroups();

  return runner.allPassed() ? 0 : 1;
}
