#pragma once

#include "FreeRTOS.h"

#include <thread>

typedef void (*TaskFunction_t)(void*);

inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*,
                                          BaseType_t) {
  return pdFAIL;  // No background tasks in reader-test
}

inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskDelay(const TickType_t xTicksToDelay) {
  std::this_thread::sleep_for(std::chrono::milliseconds(xTicksToDelay));
}

inline void taskYIELD() {
  std::this_thread::yield();
}
