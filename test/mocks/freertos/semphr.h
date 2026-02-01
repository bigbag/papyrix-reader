#pragma once

#include "FreeRTOS.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

// Mock semaphore structure
struct MockSemaphore {
  std::mutex mtx;
  std::atomic<bool> held{false};
  std::atomic<int> takeCount{0};
  std::atomic<int> giveCount{0};
};

// Global semaphore registry
inline std::vector<MockSemaphore*>& getSemaphoreRegistry() {
  static std::vector<MockSemaphore*> registry;
  return registry;
}

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  MockSemaphore* sem = new MockSemaphore();
  getSemaphoreRegistry().push_back(sem);
  return static_cast<SemaphoreHandle_t>(sem);
}

inline void vSemaphoreDelete(SemaphoreHandle_t xSemaphore) {
  MockSemaphore* sem = static_cast<MockSemaphore*>(xSemaphore);
  auto& registry = getSemaphoreRegistry();
  registry.erase(std::remove(registry.begin(), registry.end(), sem), registry.end());
  delete sem;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xBlockTime) {
  MockSemaphore* sem = static_cast<MockSemaphore*>(xSemaphore);
  if (!sem) return pdFALSE;

  if (xBlockTime == 0) {
    // Non-blocking try
    if (sem->mtx.try_lock()) {
      sem->held.store(true);
      sem->takeCount++;
      return pdTRUE;
    }
    return pdFALSE;
  } else if (xBlockTime == portMAX_DELAY) {
    // Blocking wait
    sem->mtx.lock();
    sem->held.store(true);
    sem->takeCount++;
    return pdTRUE;
  } else {
    // Timed wait
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(xBlockTime);
    while (std::chrono::steady_clock::now() < deadline) {
      if (sem->mtx.try_lock()) {
        sem->held.store(true);
        sem->takeCount++;
        return pdTRUE;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pdFALSE;
  }
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
  MockSemaphore* sem = static_cast<MockSemaphore*>(xSemaphore);
  if (!sem) return pdFALSE;

  sem->held.store(false);
  sem->giveCount++;
  sem->mtx.unlock();
  return pdTRUE;
}

// Helper to clean up semaphores after test
inline void cleanupMockSemaphores() {
  auto& registry = getSemaphoreRegistry();
  for (auto* sem : registry) {
    delete sem;
  }
  registry.clear();
}
