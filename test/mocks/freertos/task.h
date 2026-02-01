#pragma once

#include "FreeRTOS.h"

#include <atomic>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

// Task function type
typedef void (*TaskFunction_t)(void*);

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

// xTaskCreatePinnedToCore mock
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth,
                                          void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pxCreatedTask,
                                          const BaseType_t xCoreID) {
  (void)usStackDepth;
  (void)uxPriority;
  (void)xCoreID;

  MockTask* task = new MockTask();
  task->func = pvTaskCode;
  task->param = pvParameters;
  task->name = pcName ? pcName : "";

  getTaskRegistry().push_back(task);

  task->thread = std::thread([task]() {
    task->func(task->param);
    // Task should have called vTaskDelete(NULL) before exiting
  });

  *pxCreatedTask = static_cast<TaskHandle_t>(task);
  return pdPASS;
}

// vTaskDelete mock - tracks self-delete vs force-delete
inline void vTaskDelete(TaskHandle_t xTaskToDelete) {
  if (xTaskToDelete == nullptr) {
    // Self-delete (correct usage)
    getSelfDeleteCount()++;
    // Find this task and mark as self-deleted
    auto& registry = getTaskRegistry();
    std::thread::id thisId = std::this_thread::get_id();
    for (auto* t : registry) {
      if (t->thread.get_id() == thisId) {
        t->selfDeleted = true;
        t->deleted.store(true);
        break;
      }
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
}
