#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Print.h"

// ESP32 heap caps stubs
#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT 0x01
#endif
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 200000; }
inline size_t heap_caps_get_free_size(uint32_t) { return 200000; }

// PROGMEM / pgm_read helpers
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#endif

// Minimal SPISettings stub
struct SPISettings {
  SPISettings() {}
  SPISettings(uint32_t, int, int) {}
};

// Minimal SPI mock
struct MockSPI {
  void begin(int = -1, int = -1, int = -1, int = -1) {}
  void beginTransaction(const SPISettings&) {}
  void endTransaction() {}
  void transfer(uint8_t) {}
  void writeBytes(const uint8_t*, size_t) {}
};

extern MockSPI SPI;

#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 0
#endif

class String;

// Arduino GPIO and timing stubs
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
inline void delay(unsigned long) {}

#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef INPUT
#define INPUT 0
#endif
#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif

// Mock Serial for output
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

// Mock ESP class
struct MockESP {
  uint32_t getFreeHeap() { return 100000; }
  uint32_t getHeapSize() { return 320000; }
  uint32_t getMinFreeHeap() { return 80000; }
};

extern MockESP ESP;

unsigned long millis();

// Logging macros for desktop builds (bypass HWCDC dependency)
#ifndef LOG_LEVEL
#define LOG_LEVEL 2
#endif
#define ENABLE_SERIAL_LOG
#define LOG_ERR(origin, format, ...) ::printf("[ERR] [%s] " format "\n", origin, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) ::printf("[INF] [%s] " format "\n", origin, ##__VA_ARGS__)
#define LOG_DBG(origin, format, ...) ::printf("[DBG] [%s] " format "\n", origin, ##__VA_ARGS__)

// logSerial reference for desktop builds — aliases to Serial mock
static MockSerial& logSerial = Serial;

#ifdef _WIN32
#define strcasecmp _stricmp
#endif
