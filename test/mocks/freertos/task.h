#pragma once

#include "FreeRTOS.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Task function type
typedef void (*TaskFunction_t)(void*);
using StackType_t = uint32_t;

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
inline const char* pcTaskGetName(TaskHandle_t) { return "test"; }

// Mock task structure
struct MockTask {
  std::thread thread;
  TaskFunction_t func;
  void* param;
  std::string name;
  std::atomic<bool> deleted{false};
  bool selfDeleted = false;
};

// Global task registry for tracking
inline std::vector<MockTask*>& getTaskRegistry() {
  static std::vector<MockTask*> registry;
  return registry;
}

inline std::atomic<int>& getForceDeleteCount() {
  static std::atomic<int> count{0};
  return count;
}

inline std::atomic<int>& getSelfDeleteCount() {
  static std::atomic<int> count{0};
  return count;
}

inline std::atomic<bool>& getCompleteTaskBeforeCreateReturns() {
  static std::atomic<bool> enabled{false};
  return enabled;
}

struct MockTaskCreateFailureGate {
  std::mutex mutex;
  std::condition_variable cv;
  bool enabled = false;
  bool blocked = false;
  bool released = false;
};

inline MockTaskCreateFailureGate& getTaskCreateFailureGate() {
  static MockTaskCreateFailureGate gate;
  return gate;
}

inline void enableTaskCreateFailureGate() {
  auto& gate = getTaskCreateFailureGate();
  std::lock_guard<std::mutex> lock(gate.mutex);
  gate.enabled = true;
  gate.blocked = false;
  gate.released = false;
}

inline void waitForTaskCreateBlocked() {
  auto& gate = getTaskCreateFailureGate();
  std::unique_lock<std::mutex> lock(gate.mutex);
  gate.cv.wait(lock, [&gate]() { return gate.blocked; });
}

inline void releaseTaskCreateFailureGate() {
  auto& gate = getTaskCreateFailureGate();
  {
    std::lock_guard<std::mutex> lock(gate.mutex);
    gate.released = true;
  }
  gate.cv.notify_all();
}

inline thread_local MockTask* currentMockTask = nullptr;

// xTaskCreatePinnedToCore mock
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth,
                                          void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pxCreatedTask,
                                          const BaseType_t xCoreID) {
  (void)usStackDepth;
  (void)uxPriority;
  (void)xCoreID;

  auto& createGate = getTaskCreateFailureGate();
  {
    std::unique_lock<std::mutex> lock(createGate.mutex);
    if (createGate.enabled) {
      createGate.blocked = true;
      createGate.cv.notify_all();
      createGate.cv.wait(lock, [&createGate]() { return createGate.released; });
      createGate.enabled = false;
      *pxCreatedTask = nullptr;
      return pdFAIL;
    }
  }

  MockTask* task = new MockTask();
  task->func = pvTaskCode;
  task->param = pvParameters;
  task->name = pcName ? pcName : "";

  getTaskRegistry().push_back(task);
  *pxCreatedTask = static_cast<TaskHandle_t>(task);

  task->thread = std::thread([task]() {
    currentMockTask = task;
    task->func(task->param);
    currentMockTask = nullptr;
  });

  if (getCompleteTaskBeforeCreateReturns().load()) {
    while (!task->deleted.load()) std::this_thread::yield();
  }
  return pdPASS;
}

inline BaseType_t xTaskCreate(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth,
                              void* const pvParameters, UBaseType_t uxPriority,
                              TaskHandle_t* const pxCreatedTask) {
  return xTaskCreatePinnedToCore(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask, 0);
}

// vTaskDelete mock - tracks self-delete vs force-delete
inline void vTaskDelete(TaskHandle_t xTaskToDelete) {
  if (xTaskToDelete == nullptr) {
    // Self-delete (correct usage)
    getSelfDeleteCount()++;
    if (currentMockTask) {
      currentMockTask->selfDeleted = true;
      currentMockTask->deleted.store(true);
    }
  } else {
    // Force-delete (incorrect - should never happen)
    getForceDeleteCount()++;
    MockTask* task = static_cast<MockTask*>(xTaskToDelete);
    task->deleted.store(true);
  }
}

// vTaskDelay mock
inline void vTaskDelay(const TickType_t xTicksToDelay) {
  std::this_thread::sleep_for(std::chrono::milliseconds(xTicksToDelay));
}

inline void taskYIELD() {
  std::this_thread::yield();
}

// Helper to clean up tasks after test
inline void cleanupMockTasks() {
  auto& registry = getTaskRegistry();
  for (auto* task : registry) {
    if (task->thread.joinable()) {
      task->thread.join();
    }
    delete task;
  }
  registry.clear();
  getForceDeleteCount() = 0;
  getSelfDeleteCount() = 0;
  getCompleteTaskBeforeCreateReturns() = false;

  auto& createGate = getTaskCreateFailureGate();
  std::lock_guard<std::mutex> lock(createGate.mutex);
  createGate.enabled = false;
  createGate.blocked = false;
  createGate.released = false;
}
