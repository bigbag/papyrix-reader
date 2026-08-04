#include "ReadingSessionTracker.h"

#include <algorithm>
#include <climits>
#include <utility>

namespace papyrix {

void ReadingSessionTracker::begin(const std::string& path, uint32_t nowMs) {
  active_ = !path.empty();
  path_ = active_ ? path : std::string{};
  lastActivityMs_ = nowMs;
  accumulatedMs_ = 0;
  hasProgress_ = false;
  progressPercent_ = 0;
}

void ReadingSessionTracker::onActivity(uint32_t nowMs) {
  if (!active_) return;
  creditInterval(nowMs);
}

void ReadingSessionTracker::updateProgress(uint8_t progressPercent) {
  if (!active_) return;
  hasProgress_ = true;
  progressPercent_ = std::min<uint8_t>(progressPercent, 100);
}

ReadingSessionSnapshot ReadingSessionTracker::snapshot(uint32_t nowMs) const {
  ReadingSessionSnapshot result;
  if (!active_) return result;

  result.active = true;
  result.path = path_;
  result.seconds = toSeconds(liveMilliseconds(nowMs));
  result.hasProgress = hasProgress_;
  result.progressPercent = progressPercent_;
  return result;
}

ReadingSessionSnapshot ReadingSessionTracker::end(uint32_t nowMs) {
  if (!active_) return {};

  ReadingSessionSnapshot result = snapshot(nowMs);
  active_ = false;
  path_.clear();
  path_.shrink_to_fit();
  lastActivityMs_ = 0;
  accumulatedMs_ = 0;
  hasProgress_ = false;
  progressPercent_ = 0;
  return result;
}

uint32_t ReadingSessionTracker::toSeconds(uint64_t milliseconds) {
  return static_cast<uint32_t>(std::min<uint64_t>(milliseconds / 1000, UINT32_MAX));
}

uint64_t ReadingSessionTracker::liveMilliseconds(uint32_t nowMs) const {
  if (!active_) return 0;
  const uint32_t elapsed = nowMs - lastActivityMs_;
  return accumulatedMs_ + std::min(elapsed, MAX_IDLE_MS);
}

void ReadingSessionTracker::creditInterval(uint32_t nowMs) {
  const uint32_t elapsed = nowMs - lastActivityMs_;
  accumulatedMs_ += std::min(elapsed, MAX_IDLE_MS);
  lastActivityMs_ = nowMs;
}

}  // namespace papyrix
