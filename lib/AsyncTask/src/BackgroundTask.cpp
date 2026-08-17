#include "BackgroundTask.h"

#include <Logging.h>
#include <ScopedMutex.h>

#define TAG "TASK"

BackgroundTask::BackgroundTask() {
  // Create event group upfront - it must exist before task starts
  // and outlive the task for safe signaling
  eventGroup_ = xEventGroupCreate();
  lifecycleMutex_ = xSemaphoreCreateMutex();
  if (!eventGroup_ || !lifecycleMutex_) {
    LOG_ERR(TAG, "WARNING: Failed to create task synchronization primitives");
  }
}

BackgroundTask::~BackgroundTask() {
  if (!eventGroup_ || !lifecycleMutex_) {
    if (eventGroup_) vEventGroupDelete(eventGroup_);
    if (lifecycleMutex_) vSemaphoreDelete(lifecycleMutex_);
    return;
  }

  const bool stopped = stop(0);

  // The worker has finished using both synchronization primitives.
  if (eventGroup_ && stopped) {
    vEventGroupDelete(eventGroup_);
    eventGroup_ = nullptr;
  }
  if (lifecycleMutex_ && stopped) {
    vSemaphoreDelete(lifecycleMutex_);
    lifecycleMutex_ = nullptr;
  }
}

bool BackgroundTask::start(const char* name, uint32_t stackSize, TaskFunction func, int priority) {
  const char* taskName = name ? name : "?";
  if (!eventGroup_ || !lifecycleMutex_) {
    LOG_ERR(TAG, "%s: task synchronization unavailable", taskName);
    state_.store(State::ERROR, std::memory_order_release);
    return false;
  }

  uint32_t generation = 0;
  {
    ScopedMutex lock(lifecycleMutex_);
    if (!lock) {
      LOG_ERR(TAG, "%s: failed to lock task lifecycle", taskName);
      return false;
    }

    const State current = state_.load(std::memory_order_acquire);
    const uint32_t currentGeneration = generation_.load(std::memory_order_acquire);
    const bool previousExited =
        currentGeneration != 0 && exitedGeneration_.load(std::memory_order_acquire) == currentGeneration;
    const bool terminal = current == State::IDLE || current == State::COMPLETE || current == State::ERROR;
    if (current == State::STOPPING || (!terminal && !previousExited)) {
      LOG_ERR(TAG, "%s: already running (state=%d)", taskName, static_cast<int>(current));
      return false;
    }

    generation = currentGeneration + 1;
    if (generation == 0) generation = 1;
    generation_.store(generation, std::memory_order_release);
    state_.store(State::STARTING, std::memory_order_release);
    handle_.store(nullptr, std::memory_order_release);
    xEventGroupClearBits(eventGroup_, EVENT_EXITED);
    func_ = std::move(func);
    name_ = name ? name : "";
    stopRequested_.store(false, std::memory_order_release);
  }

  TaskHandle_t newHandle = nullptr;
  const BaseType_t result = xTaskCreate(&BackgroundTask::trampoline, name, stackSize, this, priority, &newHandle);

  {
    ScopedMutex lock(lifecycleMutex_);
    if (!lock) {
      LOG_ERR(TAG, "%s: failed to publish task creation", taskName);
      return false;
    }
    if (result != pdPASS || !newHandle) {
      LOG_ERR(TAG, "%s: creation failed", taskName);
      if (generation_.load(std::memory_order_acquire) == generation) {
        handle_.store(nullptr, std::memory_order_release);
        state_.store(State::ERROR, std::memory_order_release);
        exitedGeneration_.store(generation, std::memory_order_release);
        xEventGroupSetBits(eventGroup_, EVENT_EXITED);
      }
      return false;
    }
    if (generation_.load(std::memory_order_acquire) == generation &&
        exitedGeneration_.load(std::memory_order_acquire) != generation) {
      handle_.store(newHandle, std::memory_order_release);
    }
  }

  LOG_INF(TAG, "%s: started (handle=%p)", taskName, handle_.load(std::memory_order_acquire));
  return true;
}

BackgroundTask::State BackgroundTask::getState() const {
  const State state = state_.load(std::memory_order_acquire);
  if (state == State::STARTING || state == State::RUNNING) {
    const uint32_t generation = generation_.load(std::memory_order_acquire);
    if (generation != 0 && exitedGeneration_.load(std::memory_order_acquire) == generation) {
      return State::COMPLETE;
    }
  }
  return state;
}

bool BackgroundTask::isRunning() const {
  const State state = getState();
  return state == State::STARTING || state == State::RUNNING || state == State::STOPPING;
}

bool BackgroundTask::stop(uint32_t maxWaitMs) {
  if (!eventGroup_ || !lifecycleMutex_) {
    LOG_ERR(TAG, "stop: task synchronization unavailable");
    stopRequested_.store(true, std::memory_order_release);
    return false;
  }

  uint32_t targetGeneration = 0;
  std::string taskName;
  {
    ScopedMutex lock(lifecycleMutex_);
    if (!lock) return false;

    const State current = state_.load(std::memory_order_acquire);
    if (current == State::IDLE || current == State::ERROR) {
      handle_.store(nullptr, std::memory_order_release);
      return true;
    }

    targetGeneration = generation_.load(std::memory_order_acquire);
    taskName = name_.empty() ? "?" : name_;
    if (exitedGeneration_.load(std::memory_order_acquire) == targetGeneration) {
      handle_.store(nullptr, std::memory_order_release);
      state_.store(State::COMPLETE, std::memory_order_release);
      return true;
    }

    if (current != State::STOPPING) {
      state_.store(State::STOPPING, std::memory_order_release);
      stopRequested_.store(true, std::memory_order_release);
      LOG_INF(TAG, "%s: requesting stop (handle=%p)", taskName.c_str(), handle_.load(std::memory_order_acquire));
    }
  }

  const uint32_t waitStarted = millis();
  while (exitedGeneration_.load(std::memory_order_acquire) != targetGeneration) {
    TickType_t waitTicks = portMAX_DELAY;
    if (maxWaitMs != 0) {
      const uint32_t elapsed = millis() - waitStarted;
      if (elapsed >= maxWaitMs) break;
      waitTicks = pdMS_TO_TICKS(maxWaitMs - elapsed);
      if (waitTicks == 0) waitTicks = 1;
    }

    const EventBits_t bits = xEventGroupWaitBits(eventGroup_, EVENT_EXITED, pdFALSE, pdTRUE, waitTicks);
    if (!(bits & EVENT_EXITED)) break;
    if (exitedGeneration_.load(std::memory_order_acquire) != targetGeneration) {
      vTaskDelay(1);
    }
  }

  if (exitedGeneration_.load(std::memory_order_acquire) == targetGeneration) {
    ScopedMutex lock(lifecycleMutex_);
    if (lock && generation_.load(std::memory_order_acquire) == targetGeneration) {
      handle_.store(nullptr, std::memory_order_release);
      state_.store(State::COMPLETE, std::memory_order_release);
    }
    LOG_INF(TAG, "%s: generation stopped cleanly", taskName.c_str());
    return true;
  }

  LOG_ERR(TAG, "%s: WARNING - stop timeout, task may be stuck", taskName.c_str());
  LOG_ERR(TAG, "NOT force-deleting to prevent mutex corruption");
  return false;
}

void BackgroundTask::trampoline(void* param) { static_cast<BackgroundTask*>(param)->run(); }

void BackgroundTask::run() {
  const uint32_t generation = generation_.load(std::memory_order_acquire);
  State expected = State::STARTING;
  state_.compare_exchange_strong(expected, State::RUNNING, std::memory_order_acq_rel, std::memory_order_acquire);
  // Execute user function
  if (func_) {
    func_();
  }

  // The event wakes waiters; generation equality is the completion condition.
  EventGroupHandle_t eg = eventGroup_;
  if (eg) {
    xEventGroupSetBits(eg, EVENT_EXITED);
  }

  // Final access to this object. A successful stop may now transfer ownership
  // and the destructor may release synchronization primitives.
  exitedGeneration_.store(generation, std::memory_order_release);

  // Only the task's own FreeRTOS state remains to be reclaimed.
  vTaskDelete(nullptr);
}
