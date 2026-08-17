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

#include <BackgroundTask.h>

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

  // Test 3b: Restart after COMPLETE (CAS allows COMPLETE → STARTING)
  {
    cleanupMockTasks();
    BackgroundTask task;

    bool first = task.start(
        "test", 4096,
        [&]() {
          while (!task.shouldStop()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        },
        1);
    runner.expectTrue(first, "first start() returns true");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    task.stop(1000);
    runner.expectTrue(task.getState() == BackgroundTask::State::COMPLETE, "State is COMPLETE after first stop");
    // Restart uses the finalized generation even if another waiter consumed the event bit.
    xEventGroupClearBits(static_cast<EventGroupHandle_t>(getEventGroupRegistry().back()), 1);

    bool second = task.start(
        "test2", 4096,
        [&]() {
          while (!task.shouldStop()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        },
        1);
    runner.expectTrue(second, "start() after COMPLETE returns true");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runner.expectTrue(task.isRunning(), "Task is running after restart");
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

  // Test 13: Restart is rejected without blocking until the previous task has
  // published generation completion.
  {
    cleanupMockTasks();
    enableEventSetGate();
    BackgroundTask task;

    runner.expectTrue(task.start("first", 4096, []() {}, 1), "Immediate task starts");
    waitForEventSetBlocked();
    runner.expectFalse(task.start("second", 4096, []() {}, 1),
                       "Restart is rejected until the previous generation publishes completion");

    releaseEventSetGate();
    while (task.getState() != BackgroundTask::State::COMPLETE) std::this_thread::yield();
    runner.expectTrue(task.start("second", 4096, []() {}, 1),
                      "Restart succeeds after completion publication");
    runner.expectTrue(task.stop(1000), "Replacement task stops");
  }

  // Test 13b: A stop waiter must not lose the only event signal when the
  // worker is preempted between event and generation publication.
  {
    cleanupMockTasks();
    enableEventSetReturnGate();
    BackgroundTask task;

    runner.expectTrue(task.start("event-first", 4096, []() {}, 1), "Event-first task starts");
    waitForEventSetReturnBlocked();

    std::atomic<bool> stopDone{false};
    bool stopped = false;
    std::thread stopThread([&]() {
      stopped = task.stop(0);
      stopDone.store(true);
    });
    while (task.getState() != BackgroundTask::State::STOPPING) std::this_thread::yield();
    runner.expectFalse(stopDone.load(), "Stop waits for generation publication");

    releaseEventSetReturnGate();
    stopThread.join();
    runner.expectTrue(stopped, "Stop completes after generation publication");
  }

  // Test 14: A task that finishes before xTaskCreate() returns must remain
  // COMPLETE; start() must not overwrite the terminal state with RUNNING.
  {
    cleanupMockTasks();
    getCompleteTaskBeforeCreateReturns() = true;
    BackgroundTask task;

    runner.expectTrue(task.start("fast", 4096, []() {}, 1), "Fast task starts");
    getCompleteTaskBeforeCreateReturns() = false;
    runner.expectEq(static_cast<int>(BackgroundTask::State::COMPLETE), static_cast<int>(task.getState()),
                    "Completion before create return remains visible");
    runner.expectTrue(task.stop(100), "Stop observes already-completed fast task");
  }

  // Test 15: stop reserves a completed generation until its exit signal is
  // published, so start cannot replace the generation underneath the waiter.
  {
    cleanupMockTasks();
    enableEventSetGate();
    BackgroundTask task;

    runner.expectTrue(task.start("first", 4096, []() {}, 1), "Reserved-stop task starts");
    waitForEventSetBlocked();

    bool stopped = false;
    std::thread stopThread([&]() { stopped = task.stop(1000); });
    while (task.getState() != BackgroundTask::State::STOPPING) std::this_thread::yield();
    runner.expectFalse(task.start("replacement", 4096, []() {}, 1),
                       "Restart is rejected while stop owns the completed generation");

    releaseEventSetGate();
    stopThread.join();
    runner.expectTrue(stopped, "Reserved completed generation stops cleanly");
    runner.expectTrue(task.start("replacement", 4096, []() {}, 1),
                      "Restart succeeds after the stop waiter returns");
    task.stop(1000);
  }

  // Test 16: A failed xTaskCreate publishes terminal generation state so a
  // concurrent unbounded stop does not wait for a worker that does not exist.
  {
    cleanupMockTasks();
    enableTaskCreateFailureGate();
    BackgroundTask task;

    bool started = true;
    std::thread startThread([&]() { started = task.start("fail", 4096, []() {}, 1); });
    waitForTaskCreateBlocked();

    bool stopped = false;
    std::thread stopThread([&]() { stopped = task.stop(0); });
    while (task.getState() != BackgroundTask::State::STOPPING) std::this_thread::yield();

    releaseTaskCreateFailureGate();
    startThread.join();
    stopThread.join();

    runner.expectFalse(started, "Injected task creation failure is reported");
    runner.expectTrue(stopped, "Concurrent unbounded stop is released after creation failure");
    runner.expectEq(static_cast<int>(BackgroundTask::State::COMPLETE), static_cast<int>(task.getState()),
                    "Stopped failed generation is terminal");
    runner.expectEq(0, getSelfDeleteCount().load(), "Failed creation has no worker to self-delete");
    runner.expectTrue(getTaskRegistry().empty(), "Failed creation does not register a mock task");
  }

  // Test 17: A bounded stop times out without force-deleting a worker that
  // has not cooperated yet, then succeeds after the worker exits.
  {
    cleanupMockTasks();
    BackgroundTask task;
    std::atomic<bool> workerStarted{false};
    std::atomic<bool> releaseWorker{false};

    runner.expectTrue(task.start("timeout", 4096, [&]() {
      workerStarted.store(true);
      while (!releaseWorker.load()) std::this_thread::yield();
    }, 1), "Timeout fixture starts");
    while (!workerStarted.load()) std::this_thread::yield();

    const int forceDeletesBefore = getForceDeleteCount().load();
    runner.expectFalse(task.stop(5), "Bounded stop reports an uncooperative worker timeout");
    runner.expectEq(forceDeletesBefore, getForceDeleteCount().load(),
                    "Timed-out stop does not force-delete the worker");
    runner.expectTrue(task.getState() == BackgroundTask::State::STOPPING,
                      "Timed-out worker remains in STOPPING state");

    releaseWorker.store(true);
    runner.expectTrue(task.stop(1000), "Stop succeeds after the timed-out worker exits");
  }

  cleanupMockTasks();
  cleanupMockEventGroups();

  return runner.allPassed() ? 0 : 1;
}
