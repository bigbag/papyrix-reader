#include "BackgroundTask.h"

#include <Logging.h>

#define TAG "TASK"

BackgroundTask::BackgroundTask() {
  // Create event group upfront - it must exist before task starts
  // and outlive the task for safe signaling
  eventGroup_ = xEventGroupCreate();
  if (!eventGroup_) {
    LOG_ERR(TAG, "WARNING: Failed to create event group");
  }
}

BackgroundTask::~BackgroundTask() {
  stop();

  // Safe to delete event group only after task has fully exited
  if (eventGroup_) {
    vEventGroupDelete(eventGroup_);
    eventGroup_ = nullptr;
  }
}

bool BackgroundTask::start(const char* name, uint32_t stackSize, TaskFunction func, int priority) {
  // Use CAS loop to safely transition from IDLE, COMPLETE, or ERROR to STARTING
  State expected = State::IDLE;
  while (
      !state_.compare_exchange_weak(expected, State::STARTING, std::memory_order_acq_rel, std::memory_order_acquire)) {
    // Allow restart after COMPLETE or ERROR
    if (expected == State::COMPLETE || expected == State::ERROR) {
      // Try again with new expected value
      continue;
    }
    // Task is currently running or starting
    LOG_ERR(TAG, "%s: already running (state=%d)", name, static_cast<int>(expected));
    return false;
  }

  if (!eventGroup_) {
    LOG_ERR(TAG, "%s: no event group", name);
    state_.store(State::ERROR, std::memory_order_release);
    return false;
  }

  // Clear any stale exit event from previous run
  xEventGroupClearBits(eventGroup_, EVENT_EXITED);

  func_ = std::move(func);
  name_ = name ? name : "";  // Store copy to prevent use-after-free
  stopRequested_.store(false, std::memory_order_release);

  BaseType_t result = xTaskCreate(&BackgroundTask::trampoline, name, stackSize, this, priority, &handle_);

  if (result != pdPASS || !handle_) {
    LOG_ERR(TAG, "%s: creation failed", name);
    state_.store(State::ERROR, std::memory_order_release);
    return false;
  }

  state_.store(State::RUNNING, std::memory_order_release);
  LOG_INF(TAG, "%s: started (handle=%p)", name, handle_);
  return true;
}

bool BackgroundTask::stop(uint32_t maxWaitMs) {
  State current = state_.load(std::memory_order_acquire);

  // Already stopped or never started
  if (current == State::IDLE || current == State::COMPLETE || current == State::ERROR) {
    handle_ = nullptr;
    return true;
  }

  // Check event group exists (could have failed in constructor)
  if (!eventGroup_) {
    LOG_ERR(TAG, "stop: no event group, cannot wait for task");
    // Set stop flag anyway so task exits on next shouldStop() check
    stopRequested_.store(true, std::memory_order_release);
    return false;
  }

  // Signal task to stop
  state_.store(State::STOPPING, std::memory_order_release);
  stopRequested_.store(true, std::memory_order_release);

  const char* taskName = name_.empty() ? "?" : name_.c_str();
  LOG_INF(TAG, "%s: requesting stop (handle=%p)", taskName, handle_);

  // Wait for task to signal exit via event group (efficient, no polling)
  TickType_t waitTicks = (maxWaitMs == 0) ? portMAX_DELAY : pdMS_TO_TICKS(maxWaitMs);

  EventBits_t bits = xEventGroupWaitBits(eventGroup_, EVENT_EXITED,
                                         pdFALSE,  // Don't clear on exit (destructor handles)
                                         pdTRUE,   // Wait for all bits
                                         waitTicks);

  if (bits & EVENT_EXITED) {
    handle_ = nullptr;
    LOG_INF(TAG, "%s: stopped cleanly via self-delete", taskName);
    return true;
  }

  LOG_ERR(TAG, "%s: WARNING - stop timeout, task may be stuck", taskName);
  LOG_ERR(TAG, "NOT force-deleting to prevent mutex corruption");
  // DO NOT call vTaskDelete(handle_) - this causes crashes!
  return false;
}

void BackgroundTask::trampoline(void* param) { static_cast<BackgroundTask*>(param)->run(); }

void BackgroundTask::run() {
  // Execute user function
  if (func_) {
    func_();
  }

  // Update state BEFORE signaling (memory order matters)
  state_.store(State::COMPLETE, std::memory_order_release);

  // CRITICAL: Capture event group pointer locally BEFORE checking/using
  // This prevents race with destructor which could delete eventGroup_ between
  // our null check and the xEventGroupSetBits call
  EventGroupHandle_t eg = eventGroup_;

  // Signal completion via event group (stop() is waiting on this)
  // This MUST happen before vTaskDelete to avoid race condition
  if (eg) {
    xEventGroupSetBits(eg, EVENT_EXITED);
  }

  // Self-delete (FreeRTOS recommended pattern)
  // Safe: idle task will free our stack, event group already signaled
  vTaskDelete(nullptr);
}
