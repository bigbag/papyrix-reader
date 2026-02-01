#pragma once

#include <cstdint>

// Basic FreeRTOS types for host testing
typedef uint32_t TickType_t;
typedef uint32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* EventGroupHandle_t;
typedef uint32_t EventBits_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS pdTRUE
#define pdFAIL pdFALSE

#define portMAX_DELAY 0xFFFFFFFFUL
#define portTICK_PERIOD_MS 1
