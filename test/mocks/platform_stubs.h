#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <math.h>  // global ::round used by Arduino-style code

#include "Print.h"

// Keep these values equal to ESP-IDF esp_heap_caps.h.
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

struct TestHeapPool {
  size_t freeSize;
  size_t largestBlock;
};

inline TestHeapPool& testInternalHeap() {
  static TestHeapPool pool{200000, 200000};
  return pool;
}
inline TestHeapPool& testPsramHeap() {
  static TestHeapPool pool{200000, 200000};
  return pool;
}
inline bool& testPsramAllocationFailure() {
  static bool fail = false;
  return fail;
}
inline void testSetHeapPool(uint32_t caps, size_t freeSize, size_t largestBlock) {
  auto& pool = (caps & MALLOC_CAP_SPIRAM) ? testPsramHeap() : testInternalHeap();
  pool = {freeSize, largestBlock};
}
inline void testSetPsramAllocationFailure(bool fail) { testPsramAllocationFailure() = fail; }
inline void testSetLargestFreeBlock(size_t value) {
  testInternalHeap() = {value, value};
  testPsramHeap() = {value, value};
}
inline void testResetLargestFreeBlock() {
  testInternalHeap() = {200000, 200000};
  testPsramHeap() = {200000, 200000};
  testPsramAllocationFailure() = false;
}
inline size_t heap_caps_get_largest_free_block(uint32_t caps) {
  if ((caps & (MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL)) == (MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL)) return 0;
  if (caps & MALLOC_CAP_SPIRAM) return testPsramHeap().largestBlock;
  if (caps & MALLOC_CAP_INTERNAL) return testInternalHeap().largestBlock;
  return (caps & MALLOC_CAP_8BIT) ? std::max(testInternalHeap().largestBlock, testPsramHeap().largestBlock) : 0;
}
inline size_t heap_caps_get_free_size(uint32_t caps) {
  if ((caps & (MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL)) == (MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL)) return 0;
  if (caps & MALLOC_CAP_SPIRAM) return testPsramHeap().freeSize;
  if (caps & MALLOC_CAP_INTERNAL) return testInternalHeap().freeSize;
  return (caps & MALLOC_CAP_8BIT) ? testInternalHeap().freeSize + testPsramHeap().freeSize : 0;
}

// PROGMEM / pgm_read helpers for host builds
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#endif
inline void* memcpy_P(void* destination, const void* source, size_t size) {
  return std::memcpy(destination, source, size);
}

// Minimal SPISettings stub
struct SPISettings {
  SPISettings() {}
  SPISettings(uint32_t, int, int) {}
};

using TestSpiWriteHook = void (*)(const uint8_t*, size_t);
inline TestSpiWriteHook& testSpiWriteHook() {
  static TestSpiWriteHook hook = nullptr;
  return hook;
}
inline void testSetSpiWriteHook(TestSpiWriteHook hook) { testSpiWriteHook() = hook; }

// Minimal SPI mock
struct MockSPI {
  static constexpr size_t RECORD_CAPACITY = 64;

  size_t beginTransactionCount = 0;
  size_t endTransactionCount = 0;
  uint8_t transferValues[RECORD_CAPACITY]{};
  size_t transferCount = 0;
  size_t writeSizes[RECORD_CAPACITY]{};
  size_t writeCount = 0;

  void begin(int sclk = -1, int miso = -1, int mosi = -1, int ssel = -1) {
    (void)sclk;
    (void)miso;
    (void)mosi;
    (void)ssel;
  }
  void beginTransaction(const SPISettings&) { beginTransactionCount++; }
  void endTransaction() { endTransactionCount++; }
  void transfer(uint8_t value) {
    if (transferCount < RECORD_CAPACITY) transferValues[transferCount] = value;
    transferCount++;
    if (testSpiWriteHook()) testSpiWriteHook()(&value, 1);
  }
  void writeBytes(const uint8_t* data, size_t length) {
    if (testSpiWriteHook()) testSpiWriteHook()(data, length);
    if (writeCount < RECORD_CAPACITY) writeSizes[writeCount] = length;
    writeCount++;
  }
  void reset() {
    beginTransactionCount = 0;
    endTransactionCount = 0;
    transferCount = 0;
    writeCount = 0;
  }
};

extern MockSPI SPI;

// SPI mode / bit order constants
#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 0
#endif

// Forward-declare Arduino-like String used by test WString.h
class String;

// Arduino GPIO and timing stubs
enum class TestGpioEventType : uint8_t {
  PinMode,
  DigitalWrite,
  HoldDisable,
  HoldEnable,
  DeepSleepHold,
  DeepSleepHoldDisable,
  Delay
};

struct TestGpioEvent {
  TestGpioEventType type;
  int pin;
  int value;
};

constexpr size_t TEST_GPIO_EVENT_CAPACITY = 64;
extern TestGpioEvent testGpioEvents[TEST_GPIO_EVENT_CAPACITY];
extern size_t testGpioEventCount;
void testRecordGpioEvent(TestGpioEventType type, int pin, int value);
void testResetGpioEvents();

inline void pinMode(int pin, int mode) { testRecordGpioEvent(TestGpioEventType::PinMode, pin, mode); }
using TestDigitalWriteHook = void (*)(int, int);
inline TestDigitalWriteHook& testDigitalWriteHook() {
  static TestDigitalWriteHook hook = nullptr;
  return hook;
}
inline void testSetDigitalWriteHook(TestDigitalWriteHook hook) { testDigitalWriteHook() = hook; }
inline void digitalWrite(int pin, int value) {
  testRecordGpioEvent(TestGpioEventType::DigitalWrite, pin, value);
  if (testDigitalWriteHook()) testDigitalWriteHook()(pin, value);
}
using TestDigitalReadHook = int (*)(int);
inline TestDigitalReadHook& testDigitalReadHook() {
  static TestDigitalReadHook hook = nullptr;
  return hook;
}
inline void testSetDigitalReadHook(TestDigitalReadHook hook) { testDigitalReadHook() = hook; }
inline int digitalRead(int pin) { return testDigitalReadHook() ? testDigitalReadHook()(pin) : 0; }
extern bool testManualMillisEnabled;
extern unsigned long testManualMillisValue;
inline void testSetManualMillis(unsigned long value) {
  testManualMillisEnabled = true;
  testManualMillisValue = value;
}
inline void testUseRealtimeMillis() { testManualMillisEnabled = false; }
inline uint32_t& testDelayCallCount() {
  static uint32_t value = 0;
  return value;
}
inline uint32_t& testDelayTotalMs() {
  static uint32_t value = 0;
  return value;
}
inline void testResetDelayStats() {
  testDelayCallCount() = 0;
  testDelayTotalMs() = 0;
}
inline void delay(unsigned long ms) {
  testDelayCallCount()++;
  testDelayTotalMs() += static_cast<uint32_t>(ms);
  if (testManualMillisEnabled) testManualMillisValue += ms;
  testRecordGpioEvent(TestGpioEventType::Delay, -1, static_cast<int>(ms));
}
inline void delayMicroseconds(unsigned int) {}
using TestAnalogMillivoltsHook = uint32_t (*)(uint8_t);
inline TestAnalogMillivoltsHook& testAnalogMillivoltsHook() {
  static TestAnalogMillivoltsHook hook = nullptr;
  return hook;
}
inline uint32_t analogReadMilliVolts(uint8_t pin) {
  auto hook = testAnalogMillivoltsHook();
  return hook ? hook(pin) : 0;
}
inline unsigned testAnalogReadCount = 0;
using TestAnalogReadHook = int (*)(int);
inline TestAnalogReadHook& testAnalogReadHook() {
  static TestAnalogReadHook hook = nullptr;
  return hook;
}
inline void testSetAnalogReadHook(TestAnalogReadHook hook) { testAnalogReadHook() = hook; }
inline int analogRead(int pin) {
  ++testAnalogReadCount;
  return testAnalogReadHook() ? testAnalogReadHook()(pin) : 4095;
}
inline constexpr int ADC_11db = 3;
inline void analogSetAttenuation(int) {}
#ifndef F_CPU
#define F_CPU 160000000L
#endif
inline bool g_mockCpuFreqAccepted = true;
inline uint32_t g_mockCpuFreqMhz = 160;
inline bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
inline bool ledcDetach(uint8_t) { return true; }
inline bool ledcWrite(uint8_t, uint32_t) { return true; }
inline bool setCpuFrequencyMhz(uint32_t freq) {
  if (!g_mockCpuFreqAccepted) return false;
  g_mockCpuFreqMhz = freq;
  return true;
}

// Arduino constants
#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef INPUT
#define INPUT 0
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 2
#endif
#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif


// Mock Serial for test output
struct MockSerial : public Print {
  void printf(const char*, ...);
  void println(const char*);
  void println(int v);
  void println(unsigned long v);
  void println(const String& s);
  void println();
  void print(const char*);
  void print(int v);
  void print(const String& s);
  size_t write(uint8_t c) override {
    putchar(c);
    return 1;
  }
};

extern MockSerial Serial;

// Mock ESP class for ESP32-specific functions
struct MockESP {
  uint32_t getFreeHeap() { return 100000; }
  uint32_t getHeapSize() { return 320000; }
  uint32_t getMinFreeHeap() { return 80000; }
};

extern MockESP ESP;

// Host millis() declaration
unsigned long millis();

// Logging macros for test builds (bypass HWCDC dependency)
#ifndef LOG_LEVEL
#define LOG_LEVEL 2
#endif
#define ENABLE_SERIAL_LOG
#define LOG_ERR(origin, format, ...) ::printf("[ERR] [%s] " format "\n", origin, ##__VA_ARGS__)
#define LOG_WRN(origin, format, ...) ::printf("[WRN] [%s] " format "\n", origin, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) ::printf("[INF] [%s] " format "\n", origin, ##__VA_ARGS__)
#define LOG_DBG(origin, format, ...) ::printf("[DBG] [%s] " format "\n", origin, ##__VA_ARGS__)

// logSerial reference for test builds — aliases to Serial mock
static MockSerial& logSerial = Serial;

// strcasecmp for Windows
#ifdef _WIN32
#define strcasecmp _stricmp
#endif
