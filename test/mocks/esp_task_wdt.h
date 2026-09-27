#pragma once

#include <cstdint>

#ifndef ESP_OK
#define ESP_OK 0
#endif
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105

namespace MockTaskWdt {
inline int status = ESP_ERR_NOT_FOUND;
inline uint32_t elapsedMs = 0;
inline uint32_t lastResetMs = 0;
inline unsigned rejectedResets = 0;
inline bool timedOut = false;

inline void clear(int taskStatus) {
  status = taskStatus;
  elapsedMs = lastResetMs = 0;
  rejectedResets = 0;
  timedOut = false;
}

inline void advance(uint32_t milliseconds) {
  elapsedMs += milliseconds;
  if (status == ESP_OK && elapsedMs - lastResetMs > 5000) timedOut = true;
}
}  // namespace MockTaskWdt

inline int esp_task_wdt_status(void*) { return MockTaskWdt::status; }

inline int esp_task_wdt_reset() {
  if (MockTaskWdt::status != ESP_OK) {
    ++MockTaskWdt::rejectedResets;
    return MockTaskWdt::status;
  }
  MockTaskWdt::lastResetMs = MockTaskWdt::elapsedMs;
  return ESP_OK;
}
