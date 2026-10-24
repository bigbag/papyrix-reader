#include <Arduino.h>
#include <Clock.h>
#include <ClockPolicy.h>
#include <sys/time.h>

#include <cstdlib>
#include <ctime>

#include "test_utils.h"

namespace {

bool fakeBeginResult = false;
bool fakeReadResult = false;
bool fakeWriteResult = false;
int fakeReadCount = 0;
int fakeWriteCount = 0;
papyrix::board::DateTime fakeReadValue{};
papyrix::board::DateTime fakeWrittenValue{};
std::time_t systemSeconds = 1792758896;
int systemSeedCount = 0;

void resetFakeRtc() {
  fakeBeginResult = false;
  fakeReadResult = false;
  fakeWriteResult = false;
  fakeReadCount = 0;
  fakeWriteCount = 0;
  fakeReadValue = {};
  fakeWrittenValue = {};
  systemSeconds = 1792758896;
  systemSeedCount = 0;
  testSetManualMillis(0);
}

bool sameDateTime(const papyrix::board::DateTime& left, const papyrix::board::DateTime& right) {
  return left.year == right.year && left.month == right.month && left.day == right.day && left.hour == right.hour &&
         left.minute == right.minute && left.second == right.second && left.weekday == right.weekday;
}

}  // namespace

#if defined(__GLIBC__)
constexpr bool kLibcNoexcept = true;
#else
constexpr bool kLibcNoexcept = false;
#endif

extern "C" std::time_t time(std::time_t* out) noexcept(kLibcNoexcept) {
  if (out) *out = systemSeconds;
  return systemSeconds;
}

extern "C" int settimeofday(const timeval* value, const struct timezone*) noexcept(kLibcNoexcept) {
  ++systemSeedCount;
  systemSeconds = value->tv_sec;
  return 0;
}

namespace papyrix::board {

bool RtcBackend::begin() {
  available_ = fakeBeginResult;
  return available_;
}

bool RtcBackend::read(DateTime& out) {
  fakeReadCount++;
  if (!fakeReadResult) return false;
  out = fakeReadValue;
  return true;
}

bool RtcBackend::write(const DateTime& value) {
  fakeWriteCount++;
  fakeWrittenValue = value;
  return fakeWriteResult;
}

}  // namespace papyrix::board

int main() {
  TestUtils::TestRunner runner("Clock HAL");

  runner.expectTrue(papyrix::hal::clock_policy::isValid({2024, 2, 29, 23, 59, 59, 4}), "accepts a valid leap day");
  runner.expectFalse(papyrix::hal::clock_policy::isValid({2023, 2, 29, 0, 0, 0, 3}), "rejects an invalid leap day");
  runner.expectFalse(papyrix::hal::clock_policy::isValid({2024, 1, 1, 24, 0, 0, 1}), "rejects an invalid hour");
  runner.expectEq(std::time_t{0}, papyrix::hal::clock_policy::toUnixSeconds({1970, 1, 1, 0, 0, 0, 4}),
                  "converts the Unix epoch");
  runner.expectEq(std::time_t{951782400}, papyrix::hal::clock_policy::toUnixSeconds({2000, 2, 29, 0, 0, 0, 2}),
                  "converts a leap day to Unix time");

  resetFakeRtc();
  papyrix::hal::Clock unavailableClock;
  unavailableClock.init();
  runner.expectFalse(unavailableClock.isRtcAvailable(), "reports an unavailable RTC");
  papyrix::board::DateTime unavailableTime;
  runner.expectFalse(unavailableClock.readRtc(unavailableTime), "does not read an unavailable RTC");

  for (bool readSucceeds : {false, true}) {
    resetFakeRtc();
    fakeBeginResult = true;
    fakeReadResult = readSucceeds;
    fakeReadValue = {2025, 2, 29, 0, 0, 0, 6};
    papyrix::hal::Clock invalidClock;
    invalidClock.init();
    runner.expectEq(0, systemSeedCount, "invalid or unreadable RTC cannot replace system time");
    runner.expectEq(std::time_t{1792758896}, systemSeconds, "invalid retained time preserves system clock");
  }

  resetFakeRtc();
  fakeBeginResult = true;
  fakeReadResult = true;
  fakeReadValue = {2026, 8, 28, 12, 34, 56, 5};
  papyrix::hal::Clock clock;
  clock.init();
  runner.expectTrue(clock.isRtcAvailable(), "reports an available RTC");
  runner.expectEq(1, fakeReadCount, "reads the RTC during initialization");
  runner.expectEq(1, systemSeedCount, "valid retained UTC seeds system time once");

  papyrix::board::DateTime current;
  testSetManualMillis(5000);
  runner.expectTrue(clock.readRtc(current), "returns cached RTC time inside poll interval");
  runner.expectTrue(sameDateTime(fakeReadValue, current), "cached RTC time matches the last successful read");
  runner.expectEq(1, fakeReadCount, "cached read does not access the RTC");

  fakeReadResult = false;
  testSetManualMillis(10001);
  runner.expectTrue(clock.readRtc(current), "returns cached RTC time after a transient read failure");
  runner.expectTrue(sameDateTime(fakeReadValue, current), "transient failure preserves cached RTC time");
  runner.expectEq(2, fakeReadCount, "expired cache polls the RTC once");

  fakeWriteResult = true;
  systemSeconds = 1792758896;
  setenv("TZ", "GMT+5", 1);
  tzset();
  runner.expectTrue(clock.updateFromSystem(), "accepts a valid synchronized system time");
  runner.expectEq(1, fakeWriteCount, "writes synchronized system time to the RTC");
  runner.expectTrue(sameDateTime({2026, 10, 23, 12, 34, 56, 5}, fakeWrittenValue),
                    "synchronization saves UTC rather than local time");

  std::tm local{};
  runner.expectTrue(clock.localTime(local), "returns valid local system time");
  runner.expectEq(7, local.tm_hour, "local display applies timezone without changing retained UTC");

  testUseRealtimeMillis();
  return runner.allPassed() ? 0 : 1;
}
