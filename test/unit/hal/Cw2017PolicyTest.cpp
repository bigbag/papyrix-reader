#include <Arduino.h>
#include <BatteryMonitor.h>
#include <Cw2017Policy.h>
#include <Wire.h>

#include "test_utils.h"

namespace {

void primeHealthyGauge(uint8_t soc) {
  Wire.reset();
  Wire.setPresent(0x63, true);
  Wire.setRegister(0x63, 0x00, 0x0D);
  Wire.setRegister(0x63, 0x08, 0x00);
  Wire.setRegister(0x63, 0x0B, 0x80);
  for (size_t i = 0; i < papyrix::battery::kCw2017BatteryProfile.size(); ++i) {
    Wire.setRegister(0x63, static_cast<uint16_t>(0x10 + i), papyrix::battery::kCw2017BatteryProfile[i]);
  }
  Wire.setRegister(0x63, 0x02, 0x33);
  Wire.setRegister(0x63, 0x03, 0x33);
  Wire.setRegister(0x63, 0x04, soc);
  testSetManualMillis(100);
}

unsigned long socResetAt = 0;
unsigned long socSettleMs = 2500;
unsigned socResets = 0;
bool delayedSocWrite(uint8_t address, const uint8_t* data, size_t size) {
  if (size == 2) {
    Wire.setRegister(address, data[0], data[1]);
    if (data[0] == 0x08 && data[1] == 0) {
      socResetAt = millis();
      ++socResets;
    }
  }
  return true;
}

bool delayedSocRead(uint8_t address, uint8_t reg, uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    const auto current = static_cast<uint8_t>(reg + i);
    data[i] = current == 0x04 ? (millis() - socResetAt >= socSettleMs ? 67 : 255)
                              : Wire.getRegister(address, current);
  }
  return true;
}

bool versionDelayRead(uint8_t address, uint8_t reg, uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    const auto current = static_cast<uint8_t>(reg + i);
    if (current == 0x00) {
      data[i] = socResets > 0 && millis() - socResetAt < 1500 ? 0xA0 : 0x0D;
    } else if (current == 0x04) {
      data[i] = socResets > 0 ? 67 : 255;
    } else {
      data[i] = Wire.getRegister(address, current);
    }
  }
  return true;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("CW2017 policy");
  const BatteryMonitor::Cw2017Config proConfig{39, 38, 100000, 0x63,
                                             papyrix::battery::kCw2017BatteryProfile.data()};
  runner.expectTrue(papyrix::battery::cw2017VersionIsRunning(0x0D), "version 0x0D is running");
  runner.expectTrue(papyrix::battery::cw2017VersionIsRunning(0x0F), "version 0x0F is running");
  runner.expectFalse(papyrix::battery::cw2017VersionIsRunning(0xA0), "startup version is not ready");
  runner.expectEq(4096, static_cast<int>(papyrix::battery::cw2017Millivolts(0x3333)),
                  "VCELL conversion uses the OEM formula");
  Wire.reset();
  Wire.setPresent(0x63, true);
  Wire.setRegister(0x63, 0x00, 0x0D);
  Wire.setRegister(0x63, 0x0B, 0x80);
  for (size_t i = 0; i < papyrix::battery::kCw2017BatteryProfile.size(); ++i) {
    Wire.setRegister(0x63, 0x10 + i, papyrix::battery::kCw2017BatteryProfile[i]);
  }
  Wire.setRegister(0x63, 0x04, 75);
  Wire.setRegister(0x63, 0x02, 0x33);
  Wire.setRegister(0x63, 0x03, 0x33);
  testManualMillisEnabled = true;
  testManualMillisValue = 100;
  BatteryMonitor gauge(proConfig);
  auto status = gauge.readStatus();
  runner.expectTrue(status.percentageKnown && status.millivoltsKnown, "initial gauge sample is available");
  runner.expectEq(uint16_t{75}, status.percentage, "gauge reports its measured charge");
  Wire.setRegister(0x63, 0x04, 10);
  runner.expectEq(uint16_t{75}, gauge.readStatus().percentage, "rapid reads reuse the cached sample");
  testManualMillisValue = 1100;
  Wire.setPresent(0x63, false);
  status = gauge.readStatus();
  runner.expectTrue(status.percentageKnown && status.millivoltsKnown, "transient NACK retains last good sample");
  runner.expectEq(uint16_t{75}, status.percentage, "transient NACK does not report an empty battery");
  Wire.setPresent(0x63, true);
  Wire.setRegister(0x63, 0x10, 0x12);
  Wire.setRegister(0x63, 0x04, 40);
  testManualMillisValue = 2100;
  runner.expectEq(uint16_t{40}, gauge.readStatus().percentage, "polling resumes after a transient failure");
  runner.expectEq(uint8_t{0x12}, Wire.getRegister(0x63, 0x10), "sample failure does not rewrite the gauge profile");
  testManualMillisValue = 3100;
  Wire.setRegister(0x63, 0x04, 255);
  runner.expectEq(uint16_t{40}, gauge.readStatus().percentage, "invalid SOC retains last good sample");
  runner.expectEq(size_t{1}, Wire.beginCount(), "gauge polling does not reinitialize the shared I2C bus");

  // === Transient profile-read failure: no rewrite, no mode restart, later recovery ===
  Wire.reset();
  Wire.setPresent(0x63, true);
  Wire.setRegister(0x63, 0x00, 0x0D);
  Wire.setRegister(0x63, 0x08, 0x00);
  Wire.setRegister(0x63, 0x0B, 0x80);
  for (size_t i = 0; i < papyrix::battery::kCw2017BatteryProfile.size(); ++i) {
    Wire.setRegister(0x63, static_cast<uint16_t>(0x10 + i), papyrix::battery::kCw2017BatteryProfile[i]);
  }
  Wire.setRegister(0x63, 0x02, 0x33);
  Wire.setRegister(0x63, 0x03, 0x33);
  Wire.setRegister(0x63, 0x04, 75);
  Wire.setTransientReadFault(0x63, 0x10, 1);
  testManualMillisEnabled = true;
  testManualMillisValue = 100;
  BatteryMonitor recovering(proConfig);
  const auto nacked = recovering.readStatus();
  runner.expectFalse(nacked.percentageKnown, "failed profile verification reports unknown charge");
  runner.expectFalse(nacked.millivoltsKnown, "failed profile verification reports unknown voltage");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x08),
                  "failed profile read does not restart the gauge mode");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x0B),
                  "failed profile read does not rewrite the update flag");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x10), "failed profile read does not rewrite the profile");
  testManualMillisValue = 1200;
  const auto recovered = recovering.readStatus();
  runner.expectTrue(recovered.percentageKnown && recovered.millivoltsKnown,
                    "gauge recovers after a transient profile-read failure");
  runner.expectEq(uint16_t{75}, recovered.percentage, "recovered gauge reports its measured charge");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x08), "recovery does not restart the gauge mode");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x0B), "recovery does not rewrite the update flag");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x10), "recovery does not rewrite the profile");

  // === Confirmed mismatch is distinct from a read failure: repair and restart ===
  Wire.reset();
  Wire.setPresent(0x63, true);
  Wire.setRegister(0x63, 0x00, 0x0D);
  Wire.setRegister(0x63, 0x08, 0x00);
  Wire.setRegister(0x63, 0x0B, 0x80);
  for (size_t i = 0; i < papyrix::battery::kCw2017BatteryProfile.size(); ++i) {
    Wire.setRegister(0x63, static_cast<uint16_t>(0x10 + i), papyrix::battery::kCw2017BatteryProfile[i]);
  }
  Wire.setRegister(0x63, 0x10, 0x12);
  Wire.setRegister(0x63, 0x02, 0x33);
  Wire.setRegister(0x63, 0x03, 0x33);
  Wire.setRegister(0x63, 0x04, 75);
  testManualMillisValue = 100;
  BatteryMonitor repairing(proConfig);
  const auto repaired = repairing.readStatus();
  runner.expectTrue(repaired.percentageKnown, "confirmed mismatch completes initialization");
  runner.expectEq(uint16_t{75}, repaired.percentage, "repaired gauge reports its measured charge");
  runner.expectEq(uint8_t{0x50}, Wire.getRegister(0x63, 0x10), "confirmed mismatch repairs the stored profile byte");
  runner.expectEq(size_t{3}, Wire.registerWriteCount(0x63, 0x08), "confirmed mismatch restarts the gauge mode");
  runner.expectEq(size_t{1}, Wire.registerWriteCount(0x63, 0x0B), "confirmed mismatch rewrites the update flag");
  runner.expectEq(size_t{1}, Wire.registerWriteCount(0x63, 0x10), "confirmed mismatch rewrites the profile byte");

  for (bool ready : {true, false}) {
    Wire.reset();
    Wire.setPresent(0x63, true);
    Wire.setRegister(0x63, 0x00, ready ? 0x0D : 0xA0);
    Wire.setRegister(0x63, 0x0B, ready ? 0x80 : 0);
    Wire.setRegister(0x63, 0x10, 0x12);
    Wire.setRegister(0x63, 0x04, 61);
    testManualMillisValue = 100;
    BatteryMonitor classic(BatteryMonitor::Cw2017Config{39, 38});
    const auto preserved = classic.readStatus();
    runner.expectTrue(preserved.percentageKnown == ready, "Classic reports only a running factory gauge");
    if (ready) runner.expectEq(uint16_t{61}, preserved.percentage, "Classic reads factory-calibrated SOC");
    bool untouched = true;
    for (uint16_t reg = 0; reg < 0x60; ++reg) untouched &= Wire.registerWriteCount(0x63, reg) == 0;
    runner.expectTrue(untouched, "Classic never writes gauge calibration");
    runner.expectEq(uint8_t{0x12}, Wire.getRegister(0x63, 0x10), "Classic preserves a different factory table");
  }

  // === Sleeping gauge after a good init: the MODE health check recovers ===
  primeHealthyGauge(75);
  BatteryMonitor sleeping(proConfig);
  runner.expectEq(uint16_t{75}, sleeping.readStatus().percentage, "sleeping-gauge setup takes a first sample");
  Wire.setRegister(0x63, 0x08, 0x01);
  Wire.setRegister(0x63, 0x04, 72);
  testSetManualMillis(1100);
  runner.expectEq(uint16_t{75}, sleeping.readStatus().percentage,
                  "the first unhealthy MODE poll keeps the last good charge");
  testSetManualMillis(2100);
  runner.expectEq(uint16_t{75}, sleeping.readStatus().percentage,
                  "the second unhealthy MODE poll keeps the last good charge");
  testSetManualMillis(3100);
  runner.expectEq(uint16_t{72}, sleeping.readStatus().percentage,
                  "sustained unhealthy MODE restarts and recovers the gauge");
  runner.expectEq(size_t{3}, Wire.registerWriteCount(0x63, 0x08), "MODE recovery restarts the gauge mode");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x10), "MODE recovery does not rewrite a matching profile");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x0B), "MODE recovery does not rewrite the update flag");

  // === Not-running VERSION after a good init: one bounded retry, then re-init ===
  primeHealthyGauge(75);
  BatteryMonitor stalled(proConfig);
  runner.expectEq(uint16_t{75}, stalled.readStatus().percentage, "stalled-version setup takes a first sample");
  Wire.setRegister(0x63, 0x00, 0xA0);
  Wire.setRegister(0x63, 0x04, 68);
  testSetManualMillis(1100);
  stalled.readStatus();
  testSetManualMillis(2100);
  stalled.readStatus();
  testSetManualMillis(3100);
  runner.expectEq(uint16_t{75}, stalled.readStatus().percentage,
                  "a failed VERSION recovery keeps the last good charge");
  runner.expectEq(size_t{3}, Wire.registerWriteCount(0x63, 0x08), "VERSION recovery attempts one bounded restart");
  Wire.setRegister(0x63, 0x00, 0x0D);
  testSetManualMillis(4200);
  runner.expectEq(uint16_t{68}, stalled.readStatus().percentage,
                  "health sampling resumes during the retry backoff");
  testSetManualMillis(5300);
  runner.expectEq(uint16_t{68}, stalled.readStatus().percentage,
                  "a re-running version completes recovery without a second reset");
  runner.expectEq(size_t{3}, Wire.registerWriteCount(0x63, 0x08), "a re-running version needs no further mode restart");

  // === Stuck invalid SOC with healthy registers: Pro resets the gauge ===
  primeHealthyGauge(75);
  BatteryMonitor stuck(proConfig);
  runner.expectEq(uint16_t{75}, stuck.readStatus().percentage, "stuck-SOC setup takes a first sample");
  Wire.setRegister(0x63, 0x04, 255);
  testSetManualMillis(1100);
  stuck.readStatus();
  testSetManualMillis(2100);
  stuck.readStatus();
  testSetManualMillis(3100);
  runner.expectEq(uint16_t{75}, stuck.readStatus().percentage,
                  "a reset followed by an invalid SOC keeps the last good charge");
  runner.expectEq(size_t{3}, Wire.registerWriteCount(0x63, 0x08), "a stuck SOC alone triggers one bounded gauge reset");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x10), "a stuck SOC does not rewrite a matching profile");
  Wire.setRegister(0x63, 0x04, 62);
  testSetManualMillis(6200);
  runner.expectEq(uint16_t{62}, stuck.readStatus().percentage, "the gauge recovers after it computes a valid charge");
  runner.expectEq(size_t{3}, Wire.registerWriteCount(0x63, 0x08),
                  "a completed reset is not repeated after the SOC turns valid");

  // === Isolated bus fault after a good init: no reset, no profile write ===
  primeHealthyGauge(75);
  BatteryMonitor glitch(proConfig);
  runner.expectEq(uint16_t{75}, glitch.readStatus().percentage, "bus-fault setup takes a first sample");
  Wire.setTransientReadFault(0x63, 0x08, 1);
  Wire.setRegister(0x63, 0x04, 58);
  testSetManualMillis(1100);
  runner.expectEq(uint16_t{75}, glitch.readStatus().percentage, "one failed health read keeps the last good charge");
  runner.expectEq(size_t{1}, Wire.beginCount(), "an isolated fault does not rerun the bus setup");
  testSetManualMillis(2100);
  runner.expectEq(uint16_t{58}, glitch.readStatus().percentage, "the next poll resumes normal sampling");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x08), "an isolated fault does not restart the gauge");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x10), "an isolated fault does not rewrite the profile");

  // === One SOC read fault at cold init: no writes, the retry succeeds ===
  primeHealthyGauge(70);
  Wire.setTransientReadFault(0x63, 0x04, 1);
  BatteryMonitor coldFault(proConfig);
  runner.expectFalse(coldFault.readStatus().percentageKnown, "a failed SOC read at init reports an unknown charge");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x08),
                  "a failed SOC read at init does not restart the gauge");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x10),
                  "a failed SOC read at init does not write the profile");
  testSetManualMillis(1100);
  runner.expectEq(uint16_t{70}, coldFault.readStatus().percentage, "the next poll reads charge after the fault");

  primeHealthyGauge(255);
  Wire.setRegister(0x63, 0x08, 1);
  socResets = 0;
  socResetAt = millis();
  Wire.setDeviceHooks(delayedSocWrite, delayedSocRead);
  BatteryMonitor slowStartup(proConfig);
  runner.expectFalse(slowStartup.readStatus().percentageKnown, "SOC remains unknown during startup");
  for (unsigned long now : {1100UL, 2100UL}) {
    testSetManualMillis(now);
    runner.expectFalse(slowStartup.readStatus().percentageKnown, "SOC remains unknown while the gauge settles");
  }
  testSetManualMillis(3100);
  const auto settled = slowStartup.readStatus();
  runner.expectTrue(settled.percentageKnown, "a slow first SOC becomes available without another reset");
  runner.expectEq(uint16_t{67}, settled.percentage, "startup accepts the settled SOC");
  runner.expectEq(1U, socResets, "startup does not repeat the reset while SOC settles");

  primeHealthyGauge(255);
  Wire.setRegister(0x63, 0x08, 1);
  socSettleMs = 4000;
  socResets = 0;
  socResetAt = millis();
  Wire.setDeviceHooks(delayedSocWrite, delayedSocRead);
  BatteryMonitor slowSettle(proConfig);
  runner.expectFalse(slowSettle.readStatus().percentageKnown, "a 4 s settling gauge starts unknown");
  for (unsigned long now = 1100; now <= 12100; now += 1000) {
    testSetManualMillis(now);
    slowSettle.readStatus();
  }
  const auto slowSettled = slowSettle.readStatus();
  runner.expectTrue(slowSettled.percentageKnown, "SOC sampling continues during recovery backoff");
  runner.expectEq(uint16_t{67}, slowSettled.percentage, "backoff sampling accepts the settled SOC");
  const unsigned resetsAfterSettle = socResets;
  testSetManualMillis(45100);
  runner.expectEq(uint16_t{67}, slowSettle.readStatus().percentage, "the settled charge remains available");
  runner.expectEq(resetsAfterSettle, socResets, "a settled SOC cancels the pending restart");

  primeHealthyGauge(255);
  Wire.setRegister(0x63, 0x08, 1);
  socSettleMs = 400000;
  socResets = 0;
  socResetAt = millis();
  Wire.setDeviceHooks(delayedSocWrite, delayedSocRead);
  BatteryMonitor stuckStartup(proConfig);
  stuckStartup.readStatus();
  for (unsigned long now = 1100; now <= 20100; now += 1000) {
    testSetManualMillis(now);
    stuckStartup.readStatus();
  }
  runner.expectFalse(stuckStartup.readStatus().percentageKnown, "a persistently invalid SOC stays unknown");
  runner.expectTrue(socResets > 1 && socResets < 6, "a persistent SOC fault receives retries with backoff");
  // === VERSION ready 1.5 s after the restart: no second reset, recovery completes ===
  primeHealthyGauge(75);
  BatteryMonitor lateVersion(proConfig);
  runner.expectEq(uint16_t{75}, lateVersion.readStatus().percentage, "late-version setup takes a first sample");
  socResets = 0;
  socResetAt = millis();
  Wire.setDeviceHooks(delayedSocWrite, versionDelayRead);
  Wire.setRegister(0x63, 0x04, 255);
  testSetManualMillis(1100);
  lateVersion.readStatus();
  testSetManualMillis(2100);
  runner.expectEq(uint16_t{75}, lateVersion.readStatus().percentage,
                  "the invalid SOC keeps the last good charge before the restart");
  testSetManualMillis(3100);
  runner.expectEq(uint16_t{75}, lateVersion.readStatus().percentage,
                  "a restart with a slow VERSION keeps the last good charge");
  runner.expectEq(1U, socResets, "the stuck SOC triggers a single restart");
  testSetManualMillis(4100);
  runner.expectEq(uint16_t{75}, lateVersion.readStatus().percentage,
                  "the backoff window keeps the last good charge while VERSION settles");
  testSetManualMillis(5100);
  runner.expectEq(uint16_t{67}, lateVersion.readStatus().percentage,
                  "a ready VERSION completes recovery without a second reset");
  runner.expectEq(1U, socResets, "VERSION recovery observes the ready chip without a new restart");

  primeHealthyGauge(255);
  Wire.setRegister(0x63, 0x08, 1);
  socResets = 0;
  socResetAt = millis();
  Wire.setDeviceHooks(delayedSocWrite, versionDelayRead);
  BatteryMonitor coldVersion(proConfig);
  runner.expectFalse(coldVersion.readStatus().percentageKnown, "a cold gauge waits for VERSION readiness");
  testSetManualMillis(1100);
  runner.expectFalse(coldVersion.readStatus().percentageKnown, "a cold gauge keeps polling while VERSION settles");
  testSetManualMillis(2100);
  runner.expectEq(uint16_t{67}, coldVersion.readStatus().percentage, "a cold gauge accepts the ready VERSION");
  runner.expectEq(1U, socResets, "cold VERSION readiness does not repeat the completed restart");

  // === Throttled recovery: failed retries back off without a reset storm ===
  primeHealthyGauge(75);
  BatteryMonitor broken(proConfig);
  runner.expectEq(uint16_t{75}, broken.readStatus().percentage, "broken-gauge setup takes a first sample");
  Wire.setRegister(0x63, 0x00, 0xA0);
  Wire.setRegister(0x63, 0x04, 255);
  testSetManualMillis(1100);
  broken.readStatus();
  testSetManualMillis(2100);
  broken.readStatus();
  testSetManualMillis(3100);
  testResetDelayStats();
  runner.expectEq(uint16_t{75}, broken.readStatus().percentage, "a broken gauge keeps the last good charge");
  runner.expectTrue(testDelayTotalMs() <= 1100, "a failed recovery pass stays below the 1.1 s blocking ceiling");
  runner.expectEq(size_t{3}, Wire.registerWriteCount(0x63, 0x08), "the first failed recovery resets once");
  testSetManualMillis(4200);
  const auto throttled = broken.readStatus();
  runner.expectEq(uint16_t{75}, throttled.percentage, "a throttled retry keeps the last good charge");
  runner.expectEq(size_t{3}, Wire.registerWriteCount(0x63, 0x08), "a throttled retry adds no reset");
  testSetManualMillis(5300);
  runner.expectEq(uint16_t{75}, broken.readStatus().percentage,
                  "the second failed recovery keeps the last good charge");
  runner.expectEq(size_t{6}, Wire.registerWriteCount(0x63, 0x08), "the backoff window allows one retry");
  testSetManualMillis(8600);
  runner.expectEq(uint16_t{75}, broken.readStatus().percentage, "the longer backoff window keeps the last good charge");
  runner.expectEq(size_t{6}, Wire.registerWriteCount(0x63, 0x08), "a longer backoff window blocks the retry");
  testSetManualMillis(9600);
  runner.expectEq(uint16_t{75}, broken.readStatus().percentage, "the third failed recovery keeps the last good charge");
  runner.expectEq(size_t{9}, Wire.registerWriteCount(0x63, 0x08), "the expired backoff window allows the next retry");

  // === Wrap-safe timing across the millis rollover ===
  primeHealthyGauge(70);
  testSetManualMillis(~0UL - 1500);
  BatteryMonitor wrapping(proConfig);
  runner.expectEq(uint16_t{70}, wrapping.readStatus().percentage, "rollover setup takes a first sample");
  Wire.setRegister(0x63, 0x08, 0x01);
  Wire.setRegister(0x63, 0x04, 66);
  testSetManualMillis(~0UL - 500);
  runner.expectEq(uint16_t{70}, wrapping.readStatus().percentage, "the poll interval works before the rollover");
  testSetManualMillis(500);
  runner.expectEq(uint16_t{70}, wrapping.readStatus().percentage, "the unhealthy streak works across the rollover");
  testSetManualMillis(1500);
  runner.expectEq(uint16_t{66}, wrapping.readStatus().percentage, "recovery runs after the rollover");
  runner.expectEq(size_t{3}, Wire.registerWriteCount(0x63, 0x08), "rollover recovery restarts the gauge mode once");
  runner.expectEq(size_t{0}, Wire.registerWriteCount(0x63, 0x10), "rollover recovery does not rewrite the profile");

  // === Classic unhealthy gauge: re-verify only, never write ===
  primeHealthyGauge(61);
  BatteryMonitor classicSleepy(BatteryMonitor::Cw2017Config{39, 38});
  runner.expectEq(uint16_t{61}, classicSleepy.readStatus().percentage, "Classic setup takes a first factory sample");
  Wire.setRegister(0x63, 0x08, 0x01);
  Wire.setRegister(0x63, 0x04, 55);
  testSetManualMillis(1100);
  runner.expectEq(uint16_t{61}, classicSleepy.readStatus().percentage,
                  "Classic keeps its last good charge on an unhealthy gauge");
  testSetManualMillis(2100);
  runner.expectEq(uint16_t{61}, classicSleepy.readStatus().percentage,
                  "Classic keeps its last good charge on the second poll");
  testSetManualMillis(3100);
  runner.expectEq(uint16_t{61}, classicSleepy.readStatus().percentage, "Classic re-verifies the gauge without a reset");
  bool classicUntouched = true;
  for (uint16_t reg = 0; reg < 0x60; ++reg) classicUntouched &= Wire.registerWriteCount(0x63, reg) == 0;
  runner.expectTrue(classicUntouched, "Classic never receives a model write or restart sequence");
  Wire.setRegister(0x63, 0x08, 0x00);
  testSetManualMillis(4200);
  runner.expectEq(uint16_t{55}, classicSleepy.readStatus().percentage,
                  "Classic health sampling resumes during the retry backoff");
  testSetManualMillis(5300);
  runner.expectEq(uint16_t{55}, classicSleepy.readStatus().percentage,
                  "Classic resumes sampling when the gauge runs again");
  classicUntouched = true;
  for (uint16_t reg = 0; reg < 0x60; ++reg) classicUntouched &= Wire.registerWriteCount(0x63, reg) == 0;
  runner.expectTrue(classicUntouched, "Classic stays write-free through recovery");
  Wire.reset();
  testManualMillisValue = 100;
  BatteryMonitor absent(BatteryMonitor::Cw2017Config{39, 38});
  runner.expectFalse(absent.readStatus().percentageKnown, "Classic I2C failure reports unknown charge");
  testManualMillisEnabled = false;
  return runner.allPassed() ? 0 : 1;
}
