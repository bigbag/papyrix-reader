#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <string>

/**
 * Safe FreeRTOS background task wrapper with event-based signaling.
 *
 * Based on ESP32 best practices:
 * - https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_idf.html
 * - https://github.com/espressif/arduino-esp32/issues/4432
 *
 * USAGE PATTERN:
 *   BackgroundTask task;
 *   task.start("TaskName", 4096, [&task]() {
 *     while (!task.shouldStop()) {
 *       doSomeWork(task.getAbortCallback());
 *     }
 *   }, 1);
 *   task.stop();  // Always waits for self-delete, never force-kills
 *
 * KEY RULES:
 * 1. Check shouldStop() frequently (every 100ms max)
 * 2. Never hold mutexes across shouldStop() checks
 * 3. Pass getAbortCallback() to long operations
 * 4. Task ALWAYS self-deletes via vTaskDelete(NULL)
 */
class BackgroundTask {
 public:
  using TaskFunction = std::function<void()>;
  using AbortCallback = std::function<bool()>;

  /** Task lifecycle states */
  enum class State : uint8_t {
    IDLE,      // Not started or fully cleaned up
    STARTING,  // Being created
    RUNNING,   // Executing user function
    STOPPING,  // Stop requested, waiting for exit
    COMPLETE,  // Finished successfully
    ERROR      // Failed to start or crashed
  };

  BackgroundTask();
  ~BackgroundTask();

  // Non-copyable (prevent double-delete issues)
  BackgroundTask(const BackgroundTask&) = delete;
  BackgroundTask& operator=(const BackgroundTask&) = delete;

  /**
   * Start the background task.
   * @param name Task name for debugging (max 16 chars)
   * @param stackSize Stack size in bytes (use >= 4096 for complex operations)
   * @param func Task function - must check shouldStop() frequently
   * @param priority Task priority (1+ recommended, 0 = idle priority)
   */
  bool start(const char* name, uint32_t stackSize, TaskFunction func, int priority);

  /**
   * Request task to stop and wait for self-deletion.
   * Uses event-based signaling (efficient, no polling).
   * NEVER force-deletes - always waits for cooperative exit.
   * @param maxWaitMs Maximum wait time (0 = wait forever)
   * @return true if stopped cleanly, false if timeout (task still running)
   */
  bool stop(uint32_t maxWaitMs = 10000);

  /** Check if stop was requested. Call frequently in task loop. */
  bool shouldStop() const { return stopRequested_.load(std::memory_order_acquire); }

  /** Get abort callback for long-running operations. */
  AbortCallback getAbortCallback() const {
    return [this]() { return shouldStop(); };
  }

  /** Check if task is currently running. */
  bool isRunning() const;

  /** Get current task state. */
  State getState() const;

  /** Get task handle (for advanced FreeRTOS operations). */
  TaskHandle_t getHandle() const { return handle_.load(std::memory_order_acquire); }

 private:
  static void trampoline(void* param);
  void run();

  // Event bits for signaling
  static constexpr EventBits_t EVENT_EXITED = (1 << 0);

  std::atomic<TaskHandle_t> handle_{nullptr};
  EventGroupHandle_t eventGroup_ = nullptr;
  SemaphoreHandle_t lifecycleMutex_ = nullptr;
  std::atomic<bool> stopRequested_{false};
  std::atomic<State> state_{State::IDLE};
  std::atomic<uint32_t> generation_{0};
  std::atomic<uint32_t> exitedGeneration_{0};
  TaskFunction func_;
  std::string name_;  // Stored copy for debugging (prevents use-after-free)
};
