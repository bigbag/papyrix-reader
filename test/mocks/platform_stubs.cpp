#include "platform_stubs.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>

#include "LittleFS.h"
#include "WString.h"
#include "Wire.h"

// Global mock instances
MockSerial Serial;
MockSPI SPI;
MockESP ESP;
MockLittleFS LittleFS;
TwoWire Wire;
TwoWire Wire1;
TestGpioEvent testGpioEvents[TEST_GPIO_EVENT_CAPACITY]{};
size_t testGpioEventCount = 0;
bool testManualMillisEnabled = false;
unsigned long testManualMillisValue = 0;

void testRecordGpioEvent(TestGpioEventType type, int pin, int value) {
  if (testGpioEventCount >= TEST_GPIO_EVENT_CAPACITY) return;
  testGpioEvents[testGpioEventCount++] = {type, pin, value};
}

void testResetGpioEvents() {
  testGpioEventCount = 0;
  testResetDelayStats();
}

int gpio_hold_dis(int pin) {
  testRecordGpioEvent(TestGpioEventType::HoldDisable, pin, 0);
  return 0;
}

int gpio_hold_en(int pin) {
  testRecordGpioEvent(TestGpioEventType::HoldEnable, pin, 0);
  return 0;
}

void gpio_deep_sleep_hold_en() { testRecordGpioEvent(TestGpioEventType::DeepSleepHold, -1, 0); }
void gpio_deep_sleep_hold_dis() { testRecordGpioEvent(TestGpioEventType::DeepSleepHoldDisable, -1, 0); }

void MockSerial::printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void MockSerial::println(const char* s) {
  if (s)
    ::printf("%s\n", s);
  else
    ::printf("\n");
}

void MockSerial::println() { ::printf("\n"); }

void MockSerial::print(const char* s) {
  if (s) ::printf("%s", s);
}

void MockSerial::println(int v) { ::printf("%d\n", v); }

void MockSerial::println(unsigned long v) { ::printf("%lu\n", v); }

void MockSerial::print(int v) { ::printf("%d", v); }

void MockSerial::println(const String& s) {
  if (s.c_str())
    ::printf("%s\n", s.c_str());
  else
    ::printf("\n");
}

void MockSerial::print(const String& s) {
  if (s.c_str()) ::printf("%s", s.c_str());
}

unsigned long millis() {
  if (testManualMillisEnabled) return testManualMillisValue;
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}
