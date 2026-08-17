#pragma once

#include "FreeRTOS.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

// Mock event group structure
struct MockEventGroup {
  std::mutex mtx;
  std::condition_variable cv;
  std::atomic<EventBits_t> bits{0};
};

// Global event group registry
inline std::vector<MockEventGroup*>& getEventGroupRegistry() {
  static std::vector<MockEventGroup*> registry;
  return registry;
}

struct MockEventSetGate {
  std::mutex mutex;
  std::condition_variable cv;
  bool enabled = false;
  bool blocked = false;
  bool released = false;
};

inline MockEventSetGate& getEventSetGate() {
  static MockEventSetGate gate;
  return gate;
}

inline MockEventSetGate& getEventSetReturnGate() {
  static MockEventSetGate gate;
  return gate;
}

inline std::atomic<uint32_t>& getEventWaitCallCount() {
  static std::atomic<uint32_t> count{0};
  return count;
}

inline void enableEventSetGate() {
  auto& gate = getEventSetGate();
  std::lock_guard<std::mutex> lock(gate.mutex);
  gate.enabled = true;
  gate.blocked = false;
  gate.released = false;
}

inline void waitForEventSetBlocked() {
  auto& gate = getEventSetGate();
  std::unique_lock<std::mutex> lock(gate.mutex);
  gate.cv.wait(lock, [&gate]() { return gate.blocked; });
}

inline void releaseEventSetGate() {
  auto& gate = getEventSetGate();
  {
    std::lock_guard<std::mutex> lock(gate.mutex);
    gate.released = true;
  }
  gate.cv.notify_all();
}

inline void enableEventSetReturnGate() {
  auto& gate = getEventSetReturnGate();
  std::lock_guard<std::mutex> lock(gate.mutex);
  gate.enabled = true;
  gate.blocked = false;
  gate.released = false;
}

inline void waitForEventSetReturnBlocked() {
  auto& gate = getEventSetReturnGate();
  std::unique_lock<std::mutex> lock(gate.mutex);
  gate.cv.wait(lock, [&gate]() { return gate.blocked; });
}

inline void releaseEventSetReturnGate() {
  auto& gate = getEventSetReturnGate();
  {
    std::lock_guard<std::mutex> lock(gate.mutex);
    gate.released = true;
  }
  gate.cv.notify_all();
}

inline EventGroupHandle_t xEventGroupCreate() {
  MockEventGroup* eg = new MockEventGroup();
  getEventGroupRegistry().push_back(eg);
  return static_cast<EventGroupHandle_t>(eg);
}

inline void vEventGroupDelete(EventGroupHandle_t xEventGroup) {
  MockEventGroup* eg = static_cast<MockEventGroup*>(xEventGroup);
  auto& registry = getEventGroupRegistry();
  registry.erase(std::remove(registry.begin(), registry.end(), eg), registry.end());
  delete eg;
}

inline EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet) {
  MockEventGroup* eg = static_cast<MockEventGroup*>(xEventGroup);
  if (!eg) return 0;

  auto& gate = getEventSetGate();
  {
    std::unique_lock<std::mutex> lock(gate.mutex);
    if (gate.enabled) {
      gate.blocked = true;
      gate.cv.notify_all();
      gate.cv.wait(lock, [&gate]() { return gate.released; });
      gate.enabled = false;
    }
  }

  EventBits_t result;
  {
    std::lock_guard<std::mutex> lock(eg->mtx);
    eg->bits |= uxBitsToSet;
    result = eg->bits.load();
  }
  eg->cv.notify_all();

  auto& returnGate = getEventSetReturnGate();
  {
    std::unique_lock<std::mutex> lock(returnGate.mutex);
    if (returnGate.enabled) {
      returnGate.blocked = true;
      returnGate.cv.notify_all();
      returnGate.cv.wait(lock, [&returnGate]() { return returnGate.released; });
      returnGate.enabled = false;
    }
  }

  return result;
}

inline EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear) {
  MockEventGroup* eg = static_cast<MockEventGroup*>(xEventGroup);
  if (!eg) return 0;

  EventBits_t oldBits = eg->bits.load();
  eg->bits &= ~uxBitsToClear;
  return oldBits;
}

inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor,
                                       const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits,
                                       TickType_t xTicksToWait) {
  MockEventGroup* eg = static_cast<MockEventGroup*>(xEventGroup);
  if (!eg) return 0;

  getEventWaitCallCount()++;
  (void)xWaitForAllBits;  // Simplified: always OR behavior

  auto checkBits = [&]() -> bool { return (eg->bits.load() & uxBitsToWaitFor) != 0; };

  std::unique_lock<std::mutex> lock(eg->mtx);

  if (xTicksToWait == portMAX_DELAY) {
    eg->cv.wait(lock, checkBits);
  } else if (xTicksToWait > 0) {
    eg->cv.wait_for(lock, std::chrono::milliseconds(xTicksToWait), checkBits);
  }

  EventBits_t result = eg->bits.load();
  if (xClearOnExit && (result & uxBitsToWaitFor)) {
    eg->bits &= ~uxBitsToWaitFor;
  }

  return result;
}

inline EventBits_t xEventGroupGetBits(EventGroupHandle_t xEventGroup) {
  MockEventGroup* eg = static_cast<MockEventGroup*>(xEventGroup);
  if (!eg) return 0;
  return eg->bits.load();
}

// Helper to clean up event groups after test
inline void cleanupMockEventGroups() {
  auto& registry = getEventGroupRegistry();
  for (auto* eg : registry) {
    delete eg;
  }
  registry.clear();
  getEventWaitCallCount() = 0;
  auto& gate = getEventSetGate();
  {
    std::lock_guard<std::mutex> lock(gate.mutex);
    gate.enabled = false;
    gate.blocked = false;
    gate.released = false;
  }
  auto& returnGate = getEventSetReturnGate();
  std::lock_guard<std::mutex> lock(returnGate.mutex);
  returnGate.enabled = false;
  returnGate.blocked = false;
  returnGate.released = false;
}
