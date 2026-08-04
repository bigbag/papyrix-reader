#pragma once

#include <cstdint>
#include <string>

namespace papyrix {

struct ReadingSessionSnapshot {
  bool active = false;
  std::string path;
  uint32_t seconds = 0;
  bool hasProgress = false;
  uint8_t progressPercent = 0;
};

class ReadingSessionTracker {
 public:
  static constexpr uint32_t MAX_IDLE_MS = 90 * 1000;

  void begin(const std::string& path, uint32_t nowMs);
  void onActivity(uint32_t nowMs);
  void updateProgress(uint8_t progressPercent);
  ReadingSessionSnapshot snapshot(uint32_t nowMs) const;
  ReadingSessionSnapshot end(uint32_t nowMs);
  bool isActive() const { return active_; }

 private:
  static uint32_t toSeconds(uint64_t milliseconds);
  uint64_t liveMilliseconds(uint32_t nowMs) const;
  void creditInterval(uint32_t nowMs);

  bool active_ = false;
  std::string path_;
  uint32_t lastActivityMs_ = 0;
  uint64_t accumulatedMs_ = 0;
  bool hasProgress_ = false;
  uint8_t progressPercent_ = 0;
};

}  // namespace papyrix
