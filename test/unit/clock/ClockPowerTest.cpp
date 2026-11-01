#include "apps/ClockPower.h"
#include "test_utils.h"

#include <cstring>
#include <limits>

using namespace papyrix::clock_app;

int main() {
  TestUtils::TestRunner runner("Clock power policy");
  runner.expectEq(uint64_t{60000000}, nextMinuteDelayUs(1800000000000000LL), "minute_boundary: exact minute");
  runner.expectEq(uint64_t{1}, nextMinuteDelayUs(1800000059999999LL), "minute_boundary: last microsecond");
  runner.expectEq(uint64_t{0}, nextMinuteDelayUs(0), "minute_boundary: unset time cannot sleep");
  runner.expectEq(uint64_t{0}, nextMinuteDelayUs(-1), "minute_boundary: negative time cannot sleep");
  runner.expectEq(uint64_t{0}, nextMinuteDelayUs(INT64_MAX), "minute_boundary: reject overflow");
  runner.expectEq(int64_t{1800010800}, nextSyncUtc(1800000000, 10800), "sync_deadline: UTC interval");
  runner.expectEq(int64_t{0}, nextSyncUtc(1800000000, 0), "sync_deadline: disabled");
  runner.expectEq(int64_t{0}, nextSyncUtc(INT64_MAX, 10800), "sync_deadline: reject overflow");
  runner.expectEq(int64_t{10800}, nextSyncUtc(0, 10800), "unsynced time still schedules a retry");
  runner.expectEq(int64_t{0}, nextSyncUtc(-1, 10800), "failed wall-time read cannot schedule a retry");
  runner.expectFalse(interactionExpired(14999, 0, false), "keep controls before timeout");
  runner.expectTrue(interactionExpired(15000, 0, false), "standby becomes due at timeout");
  runner.expectFalse(interactionExpired(20000, 0, true), "do not interrupt an operation");
  runner.expectFalse(interactionExpired(4998, UINT32_MAX - 10000, false), "interaction_wrap: before timeout");
  runner.expectTrue(interactionExpired(4999, UINT32_MAX - 10000, false), "interaction_wrap: at timeout");

  ResumeRecord record{};
  record.active = 1;
  record.settings.powerMode = PowerMode::PowerSaving;
  record.frameValid = 1;
  record.lastFrame.utcSeconds = 1800000000;
  record.identity.imageSha256[0] = 42;
  const ResumeIdentity expected = record.identity;
  sealResume(record);
  runner.expectTrue(validateResume(record, expected), "resume_reset_precedence: valid frame");
  runner.expectEq(int(WakeRoute::Timer), int(classifyWake(record, expected, ESP_RST_DEEPSLEEP, ESP_SLEEP_WAKEUP_TIMER)),
                  "resume_reset_precedence: timer follows deep sleep");
  for (const auto wake : {ESP_SLEEP_WAKEUP_GPIO, ESP_SLEEP_WAKEUP_EXT1}) {
    runner.expectEq(int(WakeRoute::Interactive), int(classifyWake(record, expected, ESP_RST_DEEPSLEEP, wake)),
                    "resume_reset_precedence: Power returns controls");
  }
  for (const auto reset : {ESP_RST_POWERON, ESP_RST_SW, ESP_RST_TASK_WDT, ESP_RST_BROWNOUT}) {
    runner.expectEq(int(WakeRoute::NormalBoot), int(classifyWake(record, expected, reset, ESP_SLEEP_WAKEUP_TIMER)),
                    "resume_reset_precedence: resets cannot restart autonomous Clock");
  }
  auto bad = record;
  bad.settings.utcOffset = 7;
  runner.expectFalse(validateResume(bad, expected), "resume integrity rejects changed bytes");
  for (int mutation = 0; mutation < 10; ++mutation) {
    bad = record;
    switch (mutation) {
      case 0: bad.identity.board = papyrix::board::BoardId::X3; break;
      case 1: bad.identity.panel = papyrix::eink::DisplayController::UC8253; break;
      case 2: ++bad.identity.imageSha256[0]; break;
      case 3: bad.active = 0; break;
      case 4: bad.settings.powerMode = PowerMode::Normal; break;
      case 5: bad.lastFrame.utcSeconds = 0; break;
      case 6: bad.lastFrame.appearance.orientation = 4; break;
      case 7: bad.lastFrame.batteryPercent = 101; break;
      case 8: std::memset(bad.settings.ntpServers, 'a', sizeof(bad.settings.ntpServers)); break;
      case 9: bad.settings.face = papyrix::clock_faces::Face::Count; break;
    }
    sealResume(bad);
    runner.expectFalse(validateResume(bad, expected), "resume rejects incompatible or invalid values with valid CRC");
  }
  bad = record;
  ++bad.version;
  runner.expectFalse(validateResume(bad, expected), "resume rejects a different format");
  return runner.allPassed() ? 0 : 1;
}
