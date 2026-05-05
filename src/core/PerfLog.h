#pragma once

#ifndef PAPYRIX_PERF_LOG
#define PAPYRIX_PERF_LOG 0
#endif

#if PAPYRIX_PERF_LOG

#include <Logging.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#if __has_include(<Arduino.h>)
#include <Arduino.h>
inline uint32_t perfMsNow() { return millis(); }
#else
inline uint32_t perfMsNow() { return 0; }
#endif

inline void readerPerfLog(const char* phase, uint32_t startedMs, const char* fmt = nullptr, ...) {
  char suffix[128] = "";
  if (fmt) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(suffix, sizeof(suffix), fmt, args);
    va_end(args);
  }
  LOG_INF("PERF", "[PERF] %s: %lu ms%s%s", phase, static_cast<unsigned long>(perfMsNow() - startedMs),
          suffix[0] ? " " : "", suffix);
}

#else

#define readerPerfLog(...) ((void)0)
#define perfMsNow() 0u

#endif
