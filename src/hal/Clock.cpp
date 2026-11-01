#include "Clock.h"

#include <Arduino.h>
#include <Logging.h>
#include <sys/time.h>

#include "ClockPolicy.h"

#define TAG "CLOCK"

namespace papyrix::hal {
namespace {

bool setSystemTime(const board::DateTime& value) {
  const std::time_t seconds = clock_policy::toUnixSeconds(value);
  if (seconds < 0) return false;
  timeval systemTime{seconds, 0};
  return settimeofday(&systemTime, nullptr) == 0;
}

}  // namespace

void Clock::init() {
  if (initialized_) return;
  initialized_ = true;
  rtcAvailable_ = rtc_.begin();
  if (!rtcAvailable_) {
    LOG_INF(TAG, "RTC not available");
    return;
  }

  board::DateTime rtcTime;
  if (!readRtc(rtcTime)) {
    LOG_WRN(TAG, "RTC time is not valid");
    return;
  }
  if (!setSystemTime(rtcTime)) {
    LOG_WRN(TAG, "System time seed failed");
    return;
  }
  LOG_INF(TAG, "System time seeded from RTC");
}

bool Clock::readRtc(board::DateTime& out) {
  if (!rtcAvailable_) return false;

  const unsigned long now = millis();
  if (hasCachedRtc_ && now - lastRtcPollMs_ < RTC_POLL_MS) {
    out = cachedRtc_;
    return true;
  }

  board::DateTime value;
  lastRtcPollMs_ = now;
  if (rtc_.read(value) && clock_policy::isValid(value)) {
    cachedRtc_ = value;
    hasCachedRtc_ = true;
    out = value;
    return true;
  }

  if (!hasCachedRtc_) return false;
  out = cachedRtc_;
  return true;
}

bool Clock::localTime(std::tm& out) const {
  const std::time_t now = std::time(nullptr);
  if (now < 0) return false;
  return localtime_r(&now, &out) != nullptr && out.tm_year + 1900 >= 2000;
}

bool Clock::updateFromSystem() {
  const std::time_t now = std::time(nullptr);
  if (now < 0) return false;

  std::tm utc{};
  if (gmtime_r(&now, &utc) == nullptr || utc.tm_year + 1900 < 2000) return false;

  cachedRtc_ = clock_policy::fromTm(utc);
  hasCachedRtc_ = true;
  lastRtcPollMs_ = millis();
  rtcWriteFailed_ = rtcAvailable_ && !rtc_.write(cachedRtc_);
  if (rtcWriteFailed_) {
    LOG_WRN(TAG, "RTC write after NTP sync failed");
    return false;
  }
  return true;
}

}  // namespace papyrix::hal
