#include <Arduino.h>
#include <HardwareIdentity.h>
#include <Preferences.h>
#include <TargetConfig.h>

#include "hal/FrontLight.h"
#include "test_utils.h"

namespace {
using papyrix::board::HardwareIdentity;

const char* brightnessKey() { return papyrix::board::frontLightBrightnessKey(HardwareIdentity::instance().board()); }
const char* warmthKey() { return papyrix::board::frontLightWarmthKey(HardwareIdentity::instance().board()); }

void clearStore() {
  Preferences::failBegin = false;
  Preferences::failPut = false;
  Preferences::putCalls = 0;
  Preferences store;
  store.begin("frontlight");
  store.remove(brightnessKey());
  store.remove(warmthKey());
  store.end();
  Preferences::putCalls = 0;
}

void seed(const char* key, uint8_t value) {
  Preferences store;
  store.begin("frontlight");
  store.putUChar(key, value);
  store.end();
  Preferences::putCalls = 0;
}

uint8_t stored(const char* key, uint8_t fallback) {
  Preferences store;
  store.begin("frontlight", true);
  const uint8_t value = store.getUChar(key, fallback);
  store.end();
  return value;
}

uint32_t lightDuty() {
  const auto& config = HardwareIdentity::instance().profile().frontLight;
  uint32_t duty = testLedcPins[static_cast<size_t>(config.gpio)].duty;
  if (config.warmGpio != papyrix::board::kPinUnused) duty += testLedcPins[static_cast<size_t>(config.warmGpio)].duty;
  return duty;
}
uint32_t expectedDuty(uint8_t brightness, uint8_t warmth) {
  const auto& config = HardwareIdentity::instance().profile().frontLight;
  const auto duties = papyrix::board::frontLightDuties(brightness, warmth, config.resolutionBits, config.activeHigh);
  return duties.cool + duties.warm;
}
}  // namespace

int main() {
  using namespace papyrix;
  TestUtils::TestRunner runner("Front-light settings");

#if PAPYRIX_CAP_FRONTLIGHT
  const auto& frontLight = HardwareIdentity::instance().profile().frontLight;

  clearStore();
  {
    hal::FrontLight light;
    light.init(false);
    runner.expectTrue(light.isAvailable(), "front light is available");
    runner.expectEq(uint32_t{0}, lightDuty(), "first boot keeps the light off");
    runner.expectTrue(light.setBrightness(37), "brightness setting succeeds");
    runner.expectTrue(lightDuty() > 0, "nonzero brightness drives the light");
    runner.expectTrue(light.setWarmth(68), "warmth setting succeeds");
    light.shutdown(false);
  }
  {
    hal::FrontLight light;
    light.init(false);
    runner.expectEq(uint8_t{37}, light.brightness(), "brightness survives a reboot");
    runner.expectEq(uint8_t{68}, light.warmth(), "warmth survives a reboot");
    runner.expectTrue(light.setBrightness(0), "zero brightness setting succeeds");
    runner.expectEq(uint32_t{0}, lightDuty(), "zero brightness turns the light off");
    runner.expectTrue(light.setWarmth(80), "warmth can change while the light is off");
    runner.expectEq(uint32_t{0}, lightDuty(), "warmth change keeps the light off");
    light.shutdown(false);
  }
  {
    hal::FrontLight light;
    light.init(false);
    runner.expectEq(uint8_t{0}, light.brightness(), "zero brightness survives a reboot");
    runner.expectEq(uint32_t{0}, lightDuty(), "light stays off after a reboot");
    runner.expectEq(uint8_t{80}, light.warmth(), "warmth set while off survives a reboot");
    runner.expectTrue(light.setBrightness(5), "brightness can increase from zero");
    runner.expectTrue(lightDuty() > 0, "brightness increase turns the light on");
    light.shutdown(false);
  }

  clearStore();
  seed(brightnessKey(), 200);
  seed(warmthKey(), 200);
  {
    hal::FrontLight light;
    light.init(false);
    runner.expectEq(uint8_t{100}, light.brightness(), "stored brightness clamps to 100 percent");
    runner.expectEq(uint8_t{100}, light.warmth(), "stored warmth clamps to 100 percent");
    runner.expectTrue(light.setBrightness(1), "minimum nonzero brightness setting succeeds");
    runner.expectTrue(lightDuty() > 0, "minimum nonzero brightness drives the light");
    runner.expectTrue(light.setBrightness(255), "over-range brightness setting succeeds");
    runner.expectEq(uint8_t{100}, light.brightness(), "brightness setting clamps to 100 percent");
    runner.expectEq(uint8_t{100}, stored(brightnessKey(), 0xFF), "clamped brightness persists");
    runner.expectTrue(light.setWarmth(255), "over-range warmth setting succeeds");
    runner.expectEq(uint8_t{100}, stored(warmthKey(), 0xFF), "clamped warmth persists");
    light.shutdown(false);
  }

  clearStore();
  seed(brightnessKey(), 50);
  seed(warmthKey(), 50);
  {
    hal::FrontLight light;
    light.init(false);
    Preferences::failPut = true;
    runner.expectFalse(light.setBrightness(75), "brightness reports a failed save");
    runner.expectEq(uint8_t{75}, light.brightness(), "failed save keeps the live brightness");
    runner.expectEq(uint8_t{50}, stored(brightnessKey(), 0xFF), "failed save preserves stored brightness");
    runner.expectFalse(light.setWarmth(80), "warmth reports a failed save");
    runner.expectEq(uint8_t{80}, light.warmth(), "failed save keeps the live warmth");
    runner.expectEq(uint8_t{50}, stored(warmthKey(), 0xFF), "failed save preserves stored warmth");
    runner.expectFalse(light.setBrightness(0), "zero brightness reports a failed save");
    runner.expectEq(uint32_t{0}, lightDuty(), "failed zero save still turns the live light off");
    Preferences::failPut = false;
    Preferences::failBegin = true;
    runner.expectFalse(light.setBrightness(60), "brightness reports a failed store open");
    runner.expectEq(uint8_t{60}, light.brightness(), "failed store open keeps the live brightness");
    Preferences::failBegin = false;
    light.shutdown(false);
  }
  {
    hal::FrontLight light;
    light.init(false);
    runner.expectEq(uint8_t{50}, light.brightness(), "failed saves preserve brightness across reboot");
    runner.expectEq(uint8_t{50}, light.warmth(), "failed saves preserve warmth across reboot");
    runner.expectTrue(light.setBrightness(0), "zero brightness saves after storage recovers");
    runner.expectEq(uint8_t{0}, stored(brightnessKey(), 0xFF), "zero brightness persists after retry");
    light.shutdown(false);
  }

  clearStore();
  seed(brightnessKey(), 50);
  testLedcFailAttachPin = frontLight.warmGpio != board::kPinUnused ? frontLight.warmGpio : frontLight.gpio;
  {
    hal::FrontLight light;
    light.init(false);
    runner.expectFalse(light.isAvailable(), "failed PWM setup makes the light unavailable");
    runner.expectTrue(!testLedcPins[static_cast<size_t>(frontLight.gpio)].attached,
                      "failed PWM setup releases the first channel");
    Preferences::putCalls = 0;
    runner.expectFalse(light.setBrightness(20), "unavailable light refuses brightness");
    runner.expectFalse(light.setWarmth(20), "unavailable light refuses warmth");
    runner.expectEq(size_t{0}, Preferences::putCalls, "unavailable light writes no settings");
    runner.expectEq(uint8_t{50}, stored(brightnessKey(), 0xFF), "unavailable light keeps the stored brightness");
    light.shutdown(false);
  }
  testLedcFailAttachPin = -1;

  // Timeout boundary, wake restore, Off timeout, and manual zero (smoothing off).
  clearStore();
  seed(brightnessKey(), 50);
  seed(warmthKey(), 50);
  testSetManualMillis(1000);
  {
    hal::FrontLight light;
    light.init(false);
    runner.expectEq(expectedDuty(50, 50), lightDuty(), "settled light matches the selected duty");
    Preferences::putCalls = 0;
    light.update(29999, 30000, false);
    runner.expectEq(expectedDuty(50, 50), lightDuty(), "light stays on before the timeout boundary");
    light.update(30000, 30000, false);
    runner.expectEq(uint32_t{0}, lightDuty(), "light turns off at the exact timeout boundary");
    runner.expectEq(uint8_t{50}, light.brightness(), "timeout keeps the selected brightness");
    runner.expectEq(uint8_t{50}, stored(brightnessKey(), 0xFF), "timeout keeps the stored brightness");
    runner.expectEq(size_t{0}, Preferences::putCalls, "automatic off writes no settings");
    light.update(60000, 30000, false);
    runner.expectEq(uint32_t{0}, lightDuty(), "light stays off while idle grows past the timeout");
    light.update(0, 30000, false);
    runner.expectEq(expectedDuty(50, 50), lightDuty(), "activity restores the selected output");
    runner.expectEq(size_t{0}, Preferences::putCalls, "automatic restore writes no settings");
    light.update(1000000, 0, false);
    runner.expectEq(expectedDuty(50, 50), lightDuty(), "Off timeout never turns the light off");
    runner.expectTrue(light.setBrightness(0), "manual zero succeeds after timeout checks");
    light.update(0, 30000, false);
    runner.expectEq(uint32_t{0}, lightDuty(), "manual zero stays off on activity");
    light.shutdown(false);
  }

  // Asynchronous startup, timeout, wake, and manual fades (smoothing on).
  clearStore();
  seed(brightnessKey(), 50);
  seed(warmthKey(), 50);
  testSetManualMillis(10000);
  {
    hal::FrontLight light;
    light.init(true);
    runner.expectEq(uint32_t{0}, lightDuty(), "startup fade starts from off without blocking");
    testSetManualMillis(10250);
    runner.expectTrue(lightDuty() > 0 && lightDuty() < expectedDuty(50, 50), "startup fade is midway at 250 ms");
    testSetManualMillis(10500);
    runner.expectEq(expectedDuty(50, 50), lightDuty(), "startup fade reaches the selected duty at 500 ms");
    Preferences::putCalls = 0;
    light.update(30000, 30000, true);
    testSetManualMillis(10750);
    runner.expectTrue(lightDuty() > 0 && lightDuty() < expectedDuty(50, 50), "timeout fade is midway at 250 ms");
    testSetManualMillis(11000);
    runner.expectEq(uint32_t{0}, lightDuty(), "timeout fade turns the light off at 500 ms");
    runner.expectEq(uint8_t{50}, light.brightness(), "faded timeout keeps the selected brightness");
    runner.expectEq(size_t{0}, Preferences::putCalls, "faded timeout writes no settings");
    light.update(0, 30000, true);
    testSetManualMillis(11250);
    runner.expectTrue(lightDuty() > 0 && lightDuty() < expectedDuty(50, 50), "wake fade is midway at 250 ms");
    testSetManualMillis(11500);
    runner.expectEq(expectedDuty(50, 50), lightDuty(), "wake fade restores the selected duty at 500 ms");
    runner.expectTrue(light.setBrightness(0), "manual zero succeeds with smoothing");
    testSetManualMillis(11750);
    runner.expectTrue(lightDuty() > 0 && lightDuty() < expectedDuty(50, 50), "manual off fade is midway at 250 ms");
    testSetManualMillis(12000);
    runner.expectEq(uint32_t{0}, lightDuty(), "manual off fade reaches zero at 500 ms");
    runner.expectTrue(light.setBrightness(50), "manual on succeeds with smoothing");
    testSetManualMillis(12500);
    runner.expectEq(expectedDuty(50, 50), lightDuty(), "manual on fade reaches the selected duty at 500 ms");
    light.shutdown(false);
  }

  // An interrupted fade retargets from the current duty.
  clearStore();
  seed(brightnessKey(), 80);
  seed(warmthKey(), 50);
  testSetManualMillis(20000);
  {
    hal::FrontLight light;
    light.init(true);
    testSetManualMillis(20500);
    runner.expectEq(expectedDuty(80, 50), lightDuty(), "interruption test starts settled");
    runner.expectTrue(light.setBrightness(0), "fade down starts");
    testSetManualMillis(20750);
    const uint32_t midway = lightDuty();
    runner.expectTrue(midway > 0 && midway < expectedDuty(80, 50), "fade down is midway before interruption");
    runner.expectTrue(light.setBrightness(80), "interrupting brightness setting succeeds");
    runner.expectEq(midway, lightDuty(), "retarget starts from the current duty without a jump");
    testSetManualMillis(21000);
    runner.expectTrue(lightDuty() > midway && lightDuty() < expectedDuty(80, 50),
                      "retargeted fade moves from the current duty toward the new target");
    testSetManualMillis(21250);
    runner.expectEq(expectedDuty(80, 50), lightDuty(), "retargeted fade reaches the new target at 500 ms");
    light.shutdown(false);
  }

  // Smoothing toggles snap a running fade and do not start one at rest.
  clearStore();
  seed(brightnessKey(), 50);
  seed(warmthKey(), 50);
  testSetManualMillis(30000);
  {
    hal::FrontLight light;
    light.init(true);
    testSetManualMillis(30500);
    runner.expectTrue(light.setBrightness(0), "toggle test fade down starts");
    testSetManualMillis(30750);
    runner.expectTrue(lightDuty() > 0, "toggle test fade is still running");
    light.update(0, 0, false);
    runner.expectEq(uint32_t{0}, lightDuty(), "turning smoothing off snaps the fade to its target");
    light.update(0, 0, true);
    runner.expectEq(uint32_t{0}, lightDuty(), "turning smoothing on at rest starts no fade");
    runner.expectTrue(light.setBrightness(50), "brightness after toggle succeeds");
    testSetManualMillis(31000);
    runner.expectTrue(lightDuty() > 0 && lightDuty() < expectedDuty(50, 50),
                      "zero to nonzero fades after smoothing is turned on");
    testSetManualMillis(31250);
    runner.expectEq(expectedDuty(50, 50), lightDuty(), "post-toggle fade reaches the selected duty");
    light.shutdown(false);
  }

  // Low duty keeps the existing quantized scale after a fade.
  clearStore();
  seed(brightnessKey(), 1);
  seed(warmthKey(), 100);
  testSetManualMillis(40000);
  {
    hal::FrontLight light;
    light.init(true);
    testSetManualMillis(40500);
    runner.expectTrue(lightDuty() > 0 && lightDuty() == expectedDuty(1, 100),
                      "minimum brightness fade ends at the existing low duty step");
    light.shutdown(false);
  }

  // A failed fade setup falls back to the immediate target.
  clearStore();
  seed(brightnessKey(), 50);
  seed(warmthKey(), 50);
  testLedcFailFadePin = frontLight.gpio;
  testSetManualMillis(50000);
  {
    hal::FrontLight light;
    light.init(true);
    runner.expectEq(expectedDuty(50, 50), lightDuty(), "failed startup fade applies the selected duty at once");
    runner.expectTrue(light.setBrightness(0), "brightness succeeds when fade setup fails");
    runner.expectEq(uint32_t{0}, lightDuty(), "failed off fade applies zero at once");
    light.shutdown(false);
  }
  testLedcFailFadePin = -1;

  // Normal shutdown fades within one duration; forced shutdown is immediate.
  clearStore();
  seed(brightnessKey(), 50);
  seed(warmthKey(), 50);
  testSetManualMillis(60000);
  {
    hal::FrontLight light;
    light.init(true);
    testSetManualMillis(60500);
    testResetDelayStats();
    light.shutdown(true);
    runner.expectTrue(testDelayTotalMs() <= 500, "normal shutdown waits no more than one fade");
    runner.expectTrue(!testLedcPins[static_cast<size_t>(frontLight.gpio)].attached && lightDuty() == 0,
                      "normal shutdown ends off and detached");
  }
  testSetManualMillis(65000);
  {
    hal::FrontLight light;
    light.init(true);
    testSetManualMillis(65500);
    light.update(30000, 30000, true);
    testSetManualMillis(65750);
    runner.expectTrue(lightDuty() > 0, "normal sleep starts during an unfinished off transition");
    const uint32_t started = millis();
    light.shutdown(true);
    const uint32_t elapsed = static_cast<uint32_t>(millis()) - started;
    runner.expectTrue(elapsed > 0 && elapsed <= 500, "normal sleep finishes the active off fade before detaching");
    runner.expectTrue(!testLedcPins[static_cast<size_t>(frontLight.gpio)].attached && lightDuty() == 0,
                      "sleep during an off fade leaves both outputs off");
  }
  testSetManualMillis(70000);
  {
    hal::FrontLight light;
    light.init(true);
    testSetManualMillis(70500);
    runner.expectTrue(light.setBrightness(0), "forced shutdown test fade starts");
    testSetManualMillis(70750);
    runner.expectTrue(lightDuty() > 0, "forced shutdown test fade is still running");
    testResetDelayStats();
    light.shutdown(false);
    runner.expectEq(uint32_t{0}, testDelayTotalMs(), "forced shutdown does not wait");
    runner.expectTrue(!testLedcPins[static_cast<size_t>(frontLight.gpio)].attached && lightDuty() == 0,
                      "forced shutdown ends off and detached at once");
  }
  clearStore();
  seed(brightnessKey(), 50);
  seed(warmthKey(), 50);
  testSetManualMillis(UINT32_MAX - 249);
  {
    hal::FrontLight light;
    light.init(true);
    testSetManualMillis(0);
    runner.expectTrue(lightDuty() > 0 && lightDuty() < expectedDuty(50, 50),
                      "fade advances across the 32-bit clock boundary");
    testSetManualMillis(250);
    runner.expectEq(expectedDuty(50, 50), lightDuty(), "fade completes across the 32-bit clock boundary");
    light.shutdown(false);
  }
  runner.expectEq(0u, testLedcUnsafeDetachCount, "shutdown and failed setup stop fades before detaching");
  testUseRealtimeMillis();
#else
  clearStore();
  Preferences::putCalls = 0;
  hal::FrontLight light;
  light.init(false);
  runner.expectFalse(light.isAvailable(), "target without a front light reports it unavailable");
  runner.expectFalse(light.setBrightness(50), "target without a front light refuses brightness");
  runner.expectFalse(light.setWarmth(80), "target without a front light refuses warmth");
  runner.expectEq(uint8_t{0}, light.brightness(), "refused brightness keeps the light off");
  runner.expectEq(uint8_t{50}, light.warmth(), "refused warmth keeps the default warmth");
  runner.expectEq(size_t{0}, Preferences::putCalls, "target without a front light writes no settings");
  light.shutdown(false);
#endif

  return runner.allPassed() ? 0 : 1;
}
