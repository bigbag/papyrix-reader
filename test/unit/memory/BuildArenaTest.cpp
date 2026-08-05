#include "test_utils.h"

#include <BuildArena.h>

#include <cstdint>
#include <limits>

int main() {
  TestUtils::TestRunner runner("BuildArena");

  {
    BuildArena arena(256);
    runner.expectTrue(arena.valid(), "owned arena valid");
    auto* a = static_cast<uint8_t*>(arena.alloc(3, 1));
    auto* b = static_cast<uint8_t*>(arena.alloc(4, 4));
    runner.expectTrue(a != nullptr && b != nullptr, "aligned allocations succeed");
    runner.expectEq<size_t>(0, reinterpret_cast<uintptr_t>(b) % 4, "second allocation aligned");
  }

  {
    BuildArena arena(64);
    runner.expectTrue(arena.alloc(48, 1) != nullptr, "first exact-range allocation");
    runner.expectTrue(arena.alloc(32, 1) == nullptr, "oversized allocation refused");
    runner.expectEq<size_t>(48, arena.used(), "refusal leaves cursor unchanged");
    runner.expectEq<size_t>(32, arena.failedAllocSize(), "refusal size recorded");
    runner.expectTrue(arena.alloc(16, 1) != nullptr, "remaining bytes reusable");
  }

  {
    BuildArena arena(128);
    arena.alloc(16, 1);
    {
      auto outer = arena.scope();
      arena.alloc(32, 1);
      {
        auto inner = arena.scope();
        arena.alloc(32, 1);
      }
      runner.expectEq<size_t>(48, arena.used(), "inner scope rewinds");
    }
    runner.expectEq<size_t>(16, arena.used(), "outer scope rewinds");
    runner.expectEq<size_t>(80, arena.highWater(), "high water survives rewind");
  }

  {
    BuildArena first(64);
    BuildArena second(64);
    auto outer = first.scope();
    auto inner = first.scope();
    runner.expectFalse(outer.release(), "out-of-order release refused");
    runner.expectEq<uint32_t>(1, first.releaseFailures(), "invalid release counted");
    runner.expectTrue(inner.release(), "newest scope releases");
    runner.expectTrue(outer.release(), "outer releases afterward");
    auto foreign = first.scope();
    runner.expectFalse(second.release(foreign), "cross-arena release refused");
  }

  {
    alignas(std::max_align_t) uint8_t bytes[65] = {};
    BuildArena arena(bytes + 1, 64);
    void* p = arena.alloc(8, 8);
    runner.expectTrue(p != nullptr, "unaligned external buffer usable");
    runner.expectEq<size_t>(0, reinterpret_cast<uintptr_t>(p) % 8, "external allocation aligned");
  }

  {
    BuildArena arena(nullptr, 64);
    runner.expectFalse(arena.valid(), "null external arena invalid");
    runner.expectTrue(arena.alloc(1) == nullptr, "invalid arena refuses allocation");
  }

  {
    BuildArena arena(64);
    runner.expectTrue(arena.alloc(1, 3) == nullptr, "non-power-of-two alignment refused");
    runner.expectTrue(arena.allocArray<uint32_t>(std::numeric_limits<size_t>::max() / sizeof(uint32_t) + 1) == nullptr,
                      "array multiplication overflow refused");
  }

  {
    BuildArena arena(64);
    arena.noteFallback(123);
    runner.expectEq<uint32_t>(1, arena.fallbackCount(), "fallback counted");
    runner.expectEq<size_t>(123, arena.failedAllocSize(), "fallback request recorded");
    arena.reset();
    runner.expectEq<size_t>(0, arena.used(), "reset clears cursor");
    runner.expectEq<uint32_t>(1, arena.fallbackCount(), "reset preserves diagnostics");
  }

  {
    BuildArena arena(64);
    auto stale = arena.scope();
    runner.expectTrue(arena.alloc(32, 1) != nullptr, "stale setup allocation");
    arena.reset();
    runner.expectTrue(arena.alloc(16, 1) != nullptr, "post-reset allocation");
    runner.expectFalse(stale.release(), "pre-reset scope rejected");
    runner.expectEq<size_t>(16, arena.used(), "stale release does not rewind new allocation");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
