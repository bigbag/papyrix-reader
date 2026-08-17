#include "test_utils.h"

#include "drivers/Cpu.h"

#include <chrono>
#include <thread>

int main() {
  TestUtils::TestRunner runner("CpuDriverTest");

  extern uint32_t g_mockCpuFreqMhz;

  // === Default state: not throttled ===
  {
    papyrix::drivers::Cpu cpu;
    runner.expectTrue(!cpu.isThrottled(), "default: not throttled");
    runner.expectEq(uint8_t(10), cpu.loopDelayMs(), "default: active loop delay 10ms");
  }

  // === throttle() drops frequency and changes delay ===
  {
    papyrix::drivers::Cpu cpu;
    g_mockCpuFreqMhz = 160;

    cpu.throttle();

    runner.expectTrue(cpu.isThrottled(), "after throttle: isThrottled true");
    runner.expectEq(uint32_t(10), g_mockCpuFreqMhz, "after throttle: freq is 10 MHz");
    runner.expectEq(uint8_t(50), cpu.loopDelayMs(), "after throttle: idle loop delay 50ms");
  }

  // === unthrottle() restores frequency ===
  {
    papyrix::drivers::Cpu cpu;
    g_mockCpuFreqMhz = 160;

    cpu.throttle();
    cpu.unthrottle();

    runner.expectTrue(!cpu.isThrottled(), "after unthrottle: isThrottled false");
    runner.expectEq(uint32_t(160), g_mockCpuFreqMhz, "after unthrottle: freq is 160 MHz");
    runner.expectEq(uint8_t(10), cpu.loopDelayMs(), "after unthrottle: active loop delay 10ms");
  }

  // === throttle() is idempotent ===
  {
    papyrix::drivers::Cpu cpu;
    g_mockCpuFreqMhz = 160;

    cpu.throttle();
    g_mockCpuFreqMhz = 999;  // Sabotage to detect extra call
    cpu.throttle();           // Should be no-op

    runner.expectEq(uint32_t(999), g_mockCpuFreqMhz, "double throttle: no second setCpuFrequencyMhz call");
  }

  // === unthrottle() is idempotent ===
  {
    papyrix::drivers::Cpu cpu;
    g_mockCpuFreqMhz = 160;

    cpu.unthrottle();  // Already not throttled — should be no-op

    runner.expectEq(uint32_t(160), g_mockCpuFreqMhz, "unthrottle when not throttled: no setCpuFrequencyMhz call");
  }

  // === throttle -> unthrottle -> throttle cycle ===
  {
    papyrix::drivers::Cpu cpu;
    g_mockCpuFreqMhz = 160;

    cpu.throttle();
    runner.expectEq(uint32_t(10), g_mockCpuFreqMhz, "cycle: first throttle sets 10");

    cpu.unthrottle();
    runner.expectEq(uint32_t(160), g_mockCpuFreqMhz, "cycle: unthrottle restores 160");

    cpu.throttle();
    runner.expectEq(uint32_t(10), g_mockCpuFreqMhz, "cycle: second throttle sets 10 again");
  }

  // === performance lock restores and holds normal speed ===
  {
    papyrix::drivers::Cpu cpu;
    g_mockCpuFreqMhz = 160;
    cpu.throttle();
    {
      papyrix::drivers::Cpu::PerformanceLock outer(cpu);
      runner.expectEq(uint32_t(160), g_mockCpuFreqMhz, "performance lock restores 160 MHz");
      cpu.throttle();
      runner.expectFalse(cpu.isThrottled(), "throttle is suppressed while locked");
      {
        papyrix::drivers::Cpu::PerformanceLock inner(cpu);
        cpu.throttle();
        runner.expectEq(uint32_t(160), g_mockCpuFreqMhz, "nested lock keeps 160 MHz");
      }
      cpu.throttle();
      runner.expectFalse(cpu.isThrottled(), "outer lock remains authoritative");
    }
    runner.expectFalse(cpu.isThrottled(), "unlock does not force an immediate downclock");
    cpu.throttle();
    runner.expectEq(uint32_t(10), g_mockCpuFreqMhz, "idle policy can downclock after unlock");
  }

  // === ordinary early return releases lock ===
  {
    papyrix::drivers::Cpu cpu;
    g_mockCpuFreqMhz = 160;
    auto earlyReturn = [&cpu]() {
      papyrix::drivers::Cpu::PerformanceLock lock(cpu);
      return;
    };
    earlyReturn();
    cpu.throttle();
    runner.expectEq(uint32_t(10), g_mockCpuFreqMhz, "early return releases performance lock");
  }

  // === Regression: PerformanceLock acquired between throttle's flag
  // transition and its frequency write must not strand the CPU at 10 MHz.
  // The hook forces the exact interleave: the background task holds the lock
  // while the main loop is inside the throttle window. With unsynchronized
  // transitions the lock's unthrottle consumes the flag before throttle's
  // trailing check, so 10 MHz sticks; with the serialized implementation the
  // background acquisition cannot enter the window (hook times out) and
  // throttle's own trailing check restores 160 MHz.
  {
    papyrix::drivers::Cpu cpu;
    g_mockCpuFreqMhz = 160;

    std::atomic<bool> windowOpen{false};
    std::atomic<bool> lockHeld{false};
    std::atomic<bool> finish{false};

    std::thread background([&cpu, &windowOpen, &lockHeld, &finish]() {
      while (!windowOpen.load()) std::this_thread::yield();
      papyrix::drivers::Cpu::PerformanceLock lock(cpu);
      lockHeld.store(true);
      while (!finish.load()) std::this_thread::yield();
    });

    cpu.throttleCasHook = [&lockHeld, &windowOpen]() {
      windowOpen.store(true);
      // Wait for the background lock to be held. If it cannot be acquired
      // within the window (serialized implementation), proceed: throttle's
      // trailing performance-lock check must restore the active frequency.
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
      while (!lockHeld.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    };

    cpu.throttle();
    cpu.throttleCasHook = nullptr;
    finish.store(true);
    background.join();

    runner.expectTrue(!cpu.isThrottled(), "race: not throttled after interleaved lock");
    runner.expectEq(uint32_t(160), g_mockCpuFreqMhz, "race: frequency restored to 160 MHz");

    cpu.throttle();
    runner.expectEq(uint32_t(10), g_mockCpuFreqMhz, "race: normal throttle still works after");
    cpu.unthrottle();
    runner.expectEq(uint32_t(160), g_mockCpuFreqMhz, "race: unthrottle restores 160 MHz");
  }

  return runner.allPassed() ? 0 : 1;
}
