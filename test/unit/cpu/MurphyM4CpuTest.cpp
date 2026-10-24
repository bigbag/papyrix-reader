#include <Arduino.h>

#include "hal/Cpu.h"
#include "test_utils.h"

int main() {
  TestUtils::TestRunner runner("M4 fixed boot clock");
  papyrix::hal::Cpu cpu;
  g_mockCpuFreqMhz = 240;
  g_mockCpuFreqCalls = 0;
  cpu.throttle();
  runner.expectEq(uint32_t{240}, g_mockCpuFreqMhz, "idle request keeps the boot CPU frequency");
  runner.expectFalse(cpu.isThrottled(), "M4 never claims a lower CPU clock");
  runner.expectEq(uint8_t{10}, cpu.loopDelayMs(), "M4 retains active input polling");
  cpu.unthrottle();
  {
    papyrix::hal::Cpu::PerformanceLock outer(cpu);
    cpu.throttle();
    {
      papyrix::hal::Cpu::PerformanceLock inner(cpu);
      cpu.unthrottle();
      cpu.throttle();
    }
    cpu.throttle();
  }
  cpu.throttle();
  cpu.unthrottle();
  runner.expectEq(uint32_t{240}, g_mockCpuFreqMhz, "nested locks retain the boot frequency");
  runner.expectEq(unsigned{0}, g_mockCpuFreqCalls.load(), "no idle or lock transition requests a clock change");
  g_mockCpuFreqAccepted = false;
  cpu.throttle();
  cpu.unthrottle();
  runner.expectFalse(cpu.isThrottled(), "platform frequency permissions cannot change M4 polling policy");
  runner.expectEq(unsigned{0}, g_mockCpuFreqCalls.load(), "fixed-clock policy does not call a rejected frequency API");
  g_mockCpuFreqAccepted = true;
  return runner.allPassed() ? 0 : 1;
}
