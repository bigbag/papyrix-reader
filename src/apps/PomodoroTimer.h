#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace papyrix::pomodoro {

enum class Period : uint8_t { Focus, ShortBreak, LongBreak };
enum class RunState : uint8_t { Idle, Running, Paused, Waiting };
enum class StartMode : uint8_t { Manual, Auto };

inline constexpr uint32_t kFocusMs = 25u * 60u * 1000u;
inline constexpr uint32_t kShortBreakMs = 5u * 60u * 1000u;
inline constexpr uint32_t kLongBreakMs = 15u * 60u * 1000u;
inline constexpr uint8_t kFocusPerCycle = 4;

struct Timer {
  Period period = Period::Focus;
  RunState run = RunState::Idle;
  StartMode startMode = StartMode::Auto;
  uint8_t completedFocus = 0;
  uint32_t remainingMs = kFocusMs;
  uint32_t deadlineMs = 0;
};

inline uint32_t durationMs(Period period) {
  if (period == Period::ShortBreak) return kShortBreakMs;
  if (period == Period::LongBreak) return kLongBreakMs;
  return kFocusMs;
}

inline uint32_t displayedMinutes(uint32_t remainingMs) {
  if (remainingMs == 0) return 0;
  return (remainingMs + 59999u) / 60000u;
}

inline void writePeriodLabel(const Timer& timer, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  if (timer.run == RunState::Waiting) {
    const char* text = "Start focus";
    if (timer.period == Period::ShortBreak) text = "Start short break";
    if (timer.period == Period::LongBreak) text = "Start long break";
    std::snprintf(out, outSize, "%s", text);
    return;
  }
  if (timer.period == Period::ShortBreak) {
    std::snprintf(out, outSize, "Short break");
    return;
  }
  if (timer.period == Period::LongBreak) {
    std::snprintf(out, outSize, "Long break");
    return;
  }
  const unsigned session = static_cast<unsigned>(timer.completedFocus) + 1u;
  std::snprintf(out, outSize, "Focus · session %u of 4", session);
}

inline const char* statusLabel(const Timer& timer) {
  const bool automatic = timer.startMode == StartMode::Auto;
  switch (timer.run) {
    case RunState::Running:
      return automatic ? "Running · Auto" : "Running · Manual";
    case RunState::Paused:
      return automatic ? "Paused · Auto" : "Paused · Manual";
    case RunState::Waiting:
      return "Waiting · Manual";
    case RunState::Idle:
      break;
  }
  return automatic ? "Ready · Auto" : "Ready · Manual";
}

inline const char* thirdButtonLabel(const Timer& timer) {
  if (timer.run == RunState::Paused) return "Resume";
  if (timer.run == RunState::Running && timer.period != Period::Focus) return "Skip";
  if (timer.run == RunState::Running) return "Pause";
  return "Start";
}

inline void expire(Timer& timer, uint32_t nowMs) {
  if (timer.period == Period::Focus) {
    if (timer.completedFocus < kFocusPerCycle) ++timer.completedFocus;
    timer.period = timer.completedFocus == kFocusPerCycle ? Period::LongBreak : Period::ShortBreak;
  } else {
    if (timer.period == Period::LongBreak) timer.completedFocus = 0;
    timer.period = Period::Focus;
  }
  timer.remainingMs = durationMs(timer.period);
  if (timer.startMode == StartMode::Auto) {
    timer.run = RunState::Running;
    timer.deadlineMs = nowMs + timer.remainingMs;
    return;
  }
  timer.run = RunState::Waiting;
  timer.deadlineMs = 0;
}

inline bool deadlinePassed(const Timer& timer, uint32_t nowMs) {
  const uint32_t until = timer.deadlineMs - nowMs;
  return until == 0 || until > durationMs(timer.period);
}

inline void start(Timer& timer, uint32_t nowMs) {
  if (timer.run != RunState::Idle && timer.run != RunState::Waiting) return;
  if (timer.run == RunState::Idle) timer.period = Period::Focus;
  timer.remainingMs = durationMs(timer.period);
  timer.deadlineMs = nowMs + timer.remainingMs;
  timer.run = RunState::Running;
}

inline void pause(Timer& timer, uint32_t nowMs) {
  if (timer.run != RunState::Running) return;
  if (deadlinePassed(timer, nowMs)) {
    expire(timer, nowMs);
    return;
  }
  timer.remainingMs = timer.deadlineMs - nowMs;
  timer.run = RunState::Paused;
}

inline void resume(Timer& timer, uint32_t nowMs) {
  if (timer.run != RunState::Paused) return;
  timer.deadlineMs = nowMs + timer.remainingMs;
  timer.run = RunState::Running;
}

inline void reset(Timer& timer) {
  const StartMode mode = timer.startMode;
  timer = Timer{};
  timer.startMode = mode;
}

inline void skipBreak(Timer& timer, uint32_t nowMs) {
  if (timer.run != RunState::Running || timer.period == Period::Focus) return;
  expire(timer, nowMs);
}

inline void tick(Timer& timer, uint32_t nowMs) {
  if (timer.run != RunState::Running) return;
  if (!deadlinePassed(timer, nowMs)) {
    timer.remainingMs = timer.deadlineMs - nowMs;
    return;
  }
  expire(timer, nowMs);
}

}  // namespace papyrix::pomodoro
