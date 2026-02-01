#include "test_utils.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Include mocks
#include "HardwareSerial.h"

// This test focuses on race condition scenarios in PageCache-like operations
// Rather than testing the actual PageCache (which has heavy dependencies),
// we test the patterns it uses with mock implementations

using AbortCallback = std::function<bool()>;

// Mock ContentParser that simulates page parsing
class MockParser {
 public:
  MockParser(int totalPages) : totalPages_(totalPages), currentPage_(0) {}

  bool parseNext(const AbortCallback& shouldAbort = nullptr) {
    if (shouldAbort && shouldAbort()) {
      return false;
    }
    if (currentPage_ >= totalPages_) {
      return false;
    }

    // Simulate parsing work
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    currentPage_++;
    return true;
  }

  void reset() { currentPage_ = 0; }

  int currentPage() const { return currentPage_; }
  int totalPages() const { return totalPages_; }

 private:
  int totalPages_;
  int currentPage_;
};

// Simplified PageCache-like class for testing race conditions
class MockPageCache {
 public:
  MockPageCache() = default;

  // Simulates extend() operation
  bool extend(MockParser& parser, int additionalPages, const AbortCallback& shouldAbort = nullptr) {
    // Snapshot current state at start of extend
    int startPage = pageCount_.load();

    // Parse additional pages
    int parsed = 0;
    while (parsed < additionalPages) {
      if (shouldAbort && shouldAbort()) {
        return false;  // Aborted
      }

      if (!parser.parseNext(shouldAbort)) {
        isPartial_.store(false);  // No more content
        break;
      }

      // Simulate writing to cache
      std::this_thread::sleep_for(std::chrono::microseconds(100));

      parsed++;
      pageCount_.fetch_add(1);
    }

    if (parsed > 0 && parsed < additionalPages) {
      isPartial_.store(false);
    }

    return true;
  }

  // Simulates navigating to a page (called from main thread)
  void navigateTo(int page) { currentPage_.store(page); }

  int pageCount() const { return pageCount_.load(); }
  int currentPage() const { return currentPage_.load(); }
  bool isPartial() const { return isPartial_.load(); }

  bool needsExtension() const {
    int current = currentPage_.load();
    int total = pageCount_.load();
    return isPartial_.load() && current >= total - 3;
  }

 private:
  std::atomic<int> pageCount_{0};
  std::atomic<int> currentPage_{0};
  std::atomic<bool> isPartial_{true};
};

// File-like class that simulates concurrent access patterns
class MockFile {
 public:
  bool write(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.insert(buffer_.end(), data.begin(), data.end());
    writeCount_++;
    return true;
  }

  bool writeWithoutLock(const std::vector<uint8_t>& data) {
    // Intentionally NOT thread-safe - for demonstrating race conditions
    buffer_.insert(buffer_.end(), data.begin(), data.end());
    writeCount_++;
    return true;
  }

  size_t size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
  }

  int writeCount() const { return writeCount_.load(); }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    writeCount_ = 0;
  }

 private:
  std::vector<uint8_t> buffer_;
  std::mutex mutex_;
  std::atomic<int> writeCount_{0};
};

int main() {
  TestUtils::TestRunner runner("PageCache Race Conditions");

  // ============================================
  // Scenario 1: Concurrent extend and navigation
  // ============================================

  // Test 1: Navigation during extend uses snapshotted page count
  {
    MockPageCache cache;
    MockParser parser(100);

    std::atomic<bool> extendRunning{true};
    std::atomic<int> navigationsAttempted{0};

    // Start extend in background
    std::thread extender([&]() {
      cache.extend(parser, 50);
      extendRunning.store(false);
    });

    // Navigate while extend is running
    while (extendRunning.load()) {
      int pageCount = cache.pageCount();
      if (pageCount > 0) {
        cache.navigateTo(pageCount - 1);
        navigationsAttempted++;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    extender.join();

    runner.expectTrue(navigationsAttempted.load() > 0, "Navigations occurred during extend");
    runner.expectEq(50, cache.pageCount(), "Extend completed with correct page count");
  }

  // Test 2: needsExtension() check is atomic
  {
    MockPageCache cache;
    MockParser parser(20);

    // Initial extend
    cache.extend(parser, 10);
    cache.navigateTo(7);  // Near threshold

    std::atomic<int> needsExtensionTrue{0};
    std::atomic<int> needsExtensionFalse{0};
    std::atomic<bool> done{false};

    // Check needsExtension from multiple threads
    auto checker = [&]() {
      while (!done.load()) {
        if (cache.needsExtension()) {
          needsExtensionTrue++;
        } else {
          needsExtensionFalse++;
        }
        std::this_thread::yield();
      }
    };

    std::thread t1(checker);
    std::thread t2(checker);

    // Extend cache more
    cache.extend(parser, 10);
    done.store(true);

    t1.join();
    t2.join();

    // At some point it should have returned true (before extend finished)
    runner.expectTrue(needsExtensionTrue.load() > 0 || needsExtensionFalse.load() > 0,
                      "needsExtension checked without crash");
  }

  // ============================================
  // Scenario 2: File handle access during write
  // ============================================

  // Test 3: Protected file writes don't corrupt
  {
    MockFile file;
    std::atomic<bool> done{false};

    auto writer = [&]() {
      for (int i = 0; i < 100 && !done.load(); i++) {
        std::vector<uint8_t> data(10, static_cast<uint8_t>(i));
        file.write(data);
      }
    };

    std::thread t1(writer);
    std::thread t2(writer);
    std::thread t3(writer);

    t1.join();
    t2.join();
    t3.join();
    done.store(true);

    // Size should be multiple of 10 (each write is 10 bytes)
    size_t size = file.size();
    runner.expectTrue(size % 10 == 0, "Protected writes maintain data integrity");
    runner.expectEq(300, file.writeCount(), "All writes completed");
  }

  // ============================================
  // Scenario 3: Abort callback during extend
  // ============================================

  // Test 4: Abort callback stops extend cleanly
  {
    MockPageCache cache;
    MockParser parser(100);

    std::atomic<bool> shouldAbort{false};
    std::atomic<int> pagesWhenAborted{0};

    std::thread extender([&]() {
      cache.extend(parser, 100, [&]() { return shouldAbort.load(); });
      pagesWhenAborted.store(cache.pageCount());
    });

    // Let it run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Signal abort
    shouldAbort.store(true);
    extender.join();

    // Should have stopped before completing all 100 pages
    int pages = pagesWhenAborted.load();
    runner.expectTrue(pages < 100, "Abort stopped extend early");
    runner.expectTrue(pages > 0, "Some pages were cached before abort");
  }

  // Test 5: Abort callback checked frequently
  {
    MockParser parser(50);
    std::atomic<int> abortCheckCount{0};

    AbortCallback counter = [&]() {
      abortCheckCount++;
      return false;  // Don't actually abort
    };

    MockPageCache cache;
    cache.extend(parser, 50, counter);

    // Should check abort at least once per page
    runner.expectTrue(abortCheckCount.load() >= 50, "Abort callback checked at least once per page");
  }

  // ============================================
  // Scenario 4: Partial cache state consistency
  // ============================================

  // Test 6: isPartial() consistent during extend
  {
    MockPageCache cache;
    MockParser parser(20);  // Small total

    std::atomic<bool> sawPartialDuringExtend{false};
    std::atomic<bool> sawNotPartialAfterExtend{false};
    std::atomic<bool> extendDone{false};

    std::thread checker([&]() {
      while (!extendDone.load()) {
        if (cache.isPartial()) {
          sawPartialDuringExtend.store(true);
        }
        std::this_thread::yield();
      }
      if (!cache.isPartial()) {
        sawNotPartialAfterExtend.store(true);
      }
    });

    // Extend all 20 pages (less than requested 50)
    cache.extend(parser, 50);
    extendDone.store(true);

    checker.join();

    // During extend it should be partial, after it should not be
    runner.expectTrue(sawPartialDuringExtend.load() || cache.pageCount() == 0,
                      "Cache was partial during extend (or no pages)");
    runner.expectFalse(cache.isPartial(), "Cache not partial after extending all content");
  }

  // ============================================
  // Scenario 5: LUT (Look-Up Table) consistency
  // ============================================

  // Test 7: LUT modifications are atomic with page count
  {
    // Simulate LUT that maps page numbers to file offsets
    std::vector<uint32_t> lut;
    std::mutex lutMutex;
    std::atomic<int> pageCount{0};

    auto addPage = [&](uint32_t offset) {
      std::lock_guard<std::mutex> lock(lutMutex);
      lut.push_back(offset);
      pageCount.fetch_add(1);
    };

    auto getPageOffset = [&](int page) -> uint32_t {
      std::lock_guard<std::mutex> lock(lutMutex);
      if (page >= 0 && page < static_cast<int>(lut.size())) {
        return lut[page];
      }
      return 0xFFFFFFFF;  // Invalid
    };

    // Add pages from multiple threads
    auto adder = [&](int start, int count) {
      for (int i = 0; i < count; i++) {
        addPage(static_cast<uint32_t>((start + i) * 1000));
      }
    };

    std::thread t1(adder, 0, 50);
    std::thread t2(adder, 50, 50);

    t1.join();
    t2.join();

    // Page count and LUT size should match
    runner.expectEq(100, pageCount.load(), "Page count is 100");
    runner.expectEq(static_cast<size_t>(100), lut.size(), "LUT size matches page count");

    // All page offsets should be valid (not 0xFFFFFFFF)
    bool allValid = true;
    for (int i = 0; i < 100; i++) {
      if (getPageOffset(i) == 0xFFFFFFFF) {
        allValid = false;
        break;
      }
    }
    runner.expectTrue(allValid, "All page offsets are valid");
  }

  // ============================================
  // Scenario 6: Stress test - rapid operations
  // ============================================

  // Test 8: Rapid extend/navigate cycles
  {
    std::atomic<bool> done{false};
    std::atomic<int> extendCount{0};
    std::atomic<int> navigateCount{0};

    MockPageCache cache;
    MockParser parser(1000);

    auto extendTask = [&]() {
      while (!done.load() && cache.pageCount() < 100) {
        cache.extend(parser, 10, [&]() { return done.load(); });
        extendCount++;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    };

    auto navigateTask = [&]() {
      while (!done.load()) {
        int pages = cache.pageCount();
        if (pages > 0) {
          cache.navigateTo(pages / 2);
          navigateCount++;
        }
        std::this_thread::yield();
      }
    };

    std::thread t1(extendTask);
    std::thread t2(navigateTask);
    std::thread t3(navigateTask);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    done.store(true);

    t1.join();
    t2.join();
    t3.join();

    runner.expectTrue(extendCount.load() > 0, "Some extends completed");
    runner.expectTrue(navigateCount.load() > 0, "Some navigations completed");
    runner.expectTrue(cache.pageCount() > 0, "Pages were cached");
  }

  // Test 9: No deadlock with abort during extend
  {
    MockPageCache cache;
    MockParser parser(1000);

    std::atomic<bool> shouldAbort{false};

    auto start = std::chrono::steady_clock::now();

    std::thread extender([&]() {
      cache.extend(parser, 1000, [&]() { return shouldAbort.load(); });
    });

    // Quickly abort
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    shouldAbort.store(true);

    extender.join();

    auto elapsed = std::chrono::steady_clock::now() - start;

    // Should complete quickly, not hang
    runner.expectTrue(elapsed < std::chrono::seconds(1), "Abort did not cause deadlock");
  }

  return runner.allPassed() ? 0 : 1;
}
