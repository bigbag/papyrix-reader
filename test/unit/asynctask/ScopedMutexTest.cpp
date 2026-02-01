#include "test_utils.h"

#include <atomic>
#include <chrono>
#include <thread>

// Include FreeRTOS mocks
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ScopedMutex implementation (inlined for testing)
class ScopedMutex {
 public:
  explicit ScopedMutex(SemaphoreHandle_t mutex, TickType_t timeout = portMAX_DELAY)
      : mutex_(mutex), acquired_(false) {
    if (mutex_) {
      acquired_ = (xSemaphoreTake(mutex_, timeout) == pdTRUE);
    }
  }

  ~ScopedMutex() { release(); }

  ScopedMutex(const ScopedMutex&) = delete;
  ScopedMutex& operator=(const ScopedMutex&) = delete;

  ScopedMutex(ScopedMutex&& other) noexcept : mutex_(other.mutex_), acquired_(other.acquired_) {
    other.mutex_ = nullptr;
    other.acquired_ = false;
  }
  ScopedMutex& operator=(ScopedMutex&&) = delete;

  bool acquired() const { return acquired_; }
  explicit operator bool() const { return acquired_; }

  void release() {
    if (acquired_ && mutex_) {
      xSemaphoreGive(mutex_);
      acquired_ = false;
    }
  }

 private:
  SemaphoreHandle_t mutex_;
  bool acquired_;
};

// Helper macro
#define SCOPED_LOCK_IMPL2(mutex, line) ScopedMutex _scopedLock##line(mutex)
#define SCOPED_LOCK_IMPL(mutex, line) SCOPED_LOCK_IMPL2(mutex, line)
#define SCOPED_LOCK(mutex) SCOPED_LOCK_IMPL(mutex, __LINE__)

int main() {
  TestUtils::TestRunner runner("ScopedMutex");

  // ============================================
  // Basic acquisition tests
  // ============================================

  // Test 1: Successful acquisition
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    {
      ScopedMutex lock(mutex);
      runner.expectTrue(lock.acquired(), "Mutex acquired successfully");
      runner.expectTrue(static_cast<bool>(lock), "operator bool() returns true when acquired");
    }

    vSemaphoreDelete(mutex);
  }

  // Test 2: Mutex released on scope exit
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    {
      ScopedMutex lock(mutex);
      runner.expectTrue(lock.acquired(), "First lock acquired");
    }

    // After scope exit, mutex should be released - try to acquire again
    {
      ScopedMutex lock(mutex);
      runner.expectTrue(lock.acquired(), "Second lock acquired after first released");
    }

    vSemaphoreDelete(mutex);
  }

  // Test 3: Null mutex handle
  {
    ScopedMutex lock(nullptr);
    runner.expectFalse(lock.acquired(), "Null mutex - acquired() returns false");
    runner.expectFalse(static_cast<bool>(lock), "Null mutex - operator bool() returns false");
  }

  // ============================================
  // Timeout tests
  // ============================================

  // Test 4: Successful acquisition with timeout
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    {
      ScopedMutex lock(mutex, 1000);
      runner.expectTrue(lock.acquired(), "Acquisition with timeout succeeds on available mutex");
    }

    vSemaphoreDelete(mutex);
  }

  // Test 5: Timeout expiration (mutex held by another)
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    std::atomic<bool> threadStarted{false};
    std::atomic<bool> threadGotLock{false};
    std::atomic<bool> threadShouldRelease{false};

    // Hold mutex in another thread
    std::thread holder([&]() {
      ScopedMutex lock(mutex);
      threadGotLock.store(lock.acquired());
      threadStarted.store(true);
      while (!threadShouldRelease.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });

    // Wait for thread to acquire lock
    while (!threadStarted.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    runner.expectTrue(threadGotLock.load(), "Holder thread got lock");

    {
      // Try to acquire with short timeout - should fail
      auto start = std::chrono::steady_clock::now();
      ScopedMutex lock(mutex, 50);  // 50ms timeout
      auto elapsed = std::chrono::steady_clock::now() - start;

      runner.expectFalse(lock.acquired(), "Timeout - acquisition fails when mutex held");
      runner.expectTrue(elapsed >= std::chrono::milliseconds(40), "Timeout - waited approximately timeout period");
    }

    // Release holder
    threadShouldRelease.store(true);
    holder.join();

    vSemaphoreDelete(mutex);
  }

  // Test 6: Zero timeout (non-blocking try)
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    {
      // Available mutex - should succeed immediately
      ScopedMutex lock1(mutex, 0);
      runner.expectTrue(lock1.acquired(), "Zero timeout succeeds on available mutex");

      // Already held - should fail immediately
      ScopedMutex lock2(mutex, 0);
      runner.expectFalse(lock2.acquired(), "Zero timeout fails on held mutex");
    }

    vSemaphoreDelete(mutex);
  }

  // ============================================
  // Early release tests
  // ============================================

  // Test 7: Early release()
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    {
      ScopedMutex lock(mutex);
      runner.expectTrue(lock.acquired(), "Lock acquired initially");

      lock.release();
      runner.expectFalse(lock.acquired(), "acquired() returns false after release()");

      // Mutex should now be available
      ScopedMutex lock2(mutex, 0);
      runner.expectTrue(lock2.acquired(), "Another lock succeeds after early release");
    }

    vSemaphoreDelete(mutex);
  }

  // Test 8: Double release (no crash)
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    {
      ScopedMutex lock(mutex);
      lock.release();
      lock.release();  // Should be no-op, no crash

      runner.expectFalse(lock.acquired(), "Still not acquired after double release");
      runner.expectTrue(true, "Double release did not crash");
    }

    vSemaphoreDelete(mutex);
  }

  // Test 9: Release then access acquired()
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    {
      ScopedMutex lock(mutex);
      lock.release();

      // Multiple accesses after release should be safe
      runner.expectFalse(lock.acquired(), "Access 1 after release");
      runner.expectFalse(lock.acquired(), "Access 2 after release");
      runner.expectFalse(static_cast<bool>(lock), "Bool access after release");
    }

    vSemaphoreDelete(mutex);
  }

  // ============================================
  // Move semantics tests
  // ============================================

  // Test 10: Move construction transfers ownership
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    {
      ScopedMutex lock1(mutex);
      runner.expectTrue(lock1.acquired(), "Original lock acquired");

      ScopedMutex lock2(std::move(lock1));
      runner.expectFalse(lock1.acquired(), "Original lock no longer acquired after move");
      runner.expectTrue(lock2.acquired(), "New lock acquired after move");
    }

    vSemaphoreDelete(mutex);
  }

  // Test 11: Move-from object behavior
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    {
      ScopedMutex lock1(mutex);
      ScopedMutex lock2(std::move(lock1));

      // Move-from object should be safe to destroy and call methods on
      lock1.release();  // Should be no-op
      runner.expectFalse(lock1.acquired(), "Move-from object reports not acquired");
    }

    vSemaphoreDelete(mutex);
  }

  // ============================================
  // SCOPED_LOCK macro test
  // ============================================

  // Test 12: SCOPED_LOCK macro works
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    {
      SCOPED_LOCK(mutex);
      // Try to acquire same mutex with timeout 0 - should fail
      ScopedMutex lock(mutex, 0);
      runner.expectFalse(lock.acquired(), "SCOPED_LOCK holds mutex");
    }

    // After scope, should be available
    {
      ScopedMutex lock(mutex, 0);
      runner.expectTrue(lock.acquired(), "Mutex available after SCOPED_LOCK scope");
    }

    vSemaphoreDelete(mutex);
  }

  // ============================================
  // Concurrent access tests
  // ============================================

  // Test 13: Mutex protects shared resource
  {
    cleanupMockSemaphores();
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    std::atomic<int> counter{0};
    const int iterations = 100;

    auto worker = [&]() {
      for (int i = 0; i < iterations; i++) {
        ScopedMutex lock(mutex);
        if (lock.acquired()) {
          int val = counter.load();
          std::this_thread::yield();  // Encourage race conditions
          counter.store(val + 1);
        }
      }
    };

    {
      std::thread t1(worker);
      std::thread t2(worker);
      std::thread t3(worker);

      t1.join();
      t2.join();
      t3.join();
    }

    runner.expectEq(iterations * 3, counter.load(), "Mutex protected counter increments");

    vSemaphoreDelete(mutex);
  }

  cleanupMockSemaphores();

  return runner.allPassed() ? 0 : 1;
}
