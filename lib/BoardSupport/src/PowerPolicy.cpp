#include "PowerPolicy.h"

#include <Arduino.h>
#include <SDPowerControl.h>
#include <driver/gpio.h>

#include "TargetConfig.h"

namespace papyrix::board {

void releaseDeepSleepHolds() { gpio_deep_sleep_hold_dis(); }

void prepareDeepSleepPins(const BoardProfile& profile, bool externalPower) {
  if (profile.family == McuFamily::Esp32S3) {
    const auto holdOutput = [](int8_t pin, bool high) {
      if (pin == kPinUnused) return;
      gpio_hold_dis(static_cast<gpio_num_t>(pin));
      pinMode(pin, OUTPUT);
      digitalWrite(pin, high ? HIGH : LOW);
      gpio_hold_en(static_cast<gpio_num_t>(pin));
    };
    holdOutput(profile.touch.rst, false);
    holdOutput(profile.touch.powerPin, !profile.touch.powerActiveHigh);
    for (const int8_t pin : {profile.storage.sdmmcClk, profile.storage.sdmmcCmd, profile.storage.sdmmcDat0}) {
      gpio_hold_dis(static_cast<gpio_num_t>(pin));
      pinMode(pin, INPUT);
    }
    holdOutput(profile.storage.powerPin, !profile.storage.powerActiveHigh);
    holdOutput(profile.display.rst, true);
    holdOutput(profile.power.latchPin, profile.power.latchActiveHigh);
    holdOutput(profile.frontLight.gpio, !profile.frontLight.activeHigh);
    holdOutput(profile.frontLight.warmGpio, !profile.frontLight.activeHigh);
    return;
  }
  if (profile.storage.transport == StorageTransport::Spi && profile.storage.powerPin != kPinUnused) {
    papyrix::sd::disableSdPower(profile.storage.powerPin);
    gpio_hold_en(static_cast<gpio_num_t>(profile.storage.powerPin));
  }
  if (profile.power.latchPin != kPinUnused) {
    const auto latch = static_cast<gpio_num_t>(profile.power.latchPin);
    gpio_hold_dis(latch);
    pinMode(profile.power.latchPin, OUTPUT);
    const bool keepPowered = externalPower ? profile.power.latchActiveHigh : !profile.power.latchActiveHigh;
    digitalWrite(profile.power.latchPin, keepPowered ? HIGH : LOW);
    gpio_hold_en(latch);
  }

  if (profile.id != BoardId::X3) return;
  gpio_hold_dis(static_cast<gpio_num_t>(profile.display.rst));
  pinMode(profile.display.rst, OUTPUT);
  digitalWrite(profile.display.rst, HIGH);
  gpio_hold_en(static_cast<gpio_num_t>(profile.display.rst));
}

void shutdownRecoveryRails(const BoardProfile& profile) {
  if (profile.display.power != kPinUnused) {
    pinMode(profile.display.power, OUTPUT);
    digitalWrite(profile.display.power, LOW);
  }
  if (profile.storage.transport == StorageTransport::Spi && profile.storage.powerPin != kPinUnused) {
    papyrix::sd::disableSdPower(profile.storage.powerPin);
  } else if (profile.storage.powerPin != kPinUnused) {
    for (const int pin : {profile.storage.sdmmcClk, profile.storage.sdmmcCmd, profile.storage.sdmmcDat0}) {
      pinMode(pin, INPUT);
    }
    pinMode(profile.storage.powerPin, OUTPUT);
    digitalWrite(profile.storage.powerPin, profile.storage.powerActiveHigh ? LOW : HIGH);
  }
}

void restoreRecoveryStorage(const BoardProfile& profile) {
  if (profile.storage.transport == StorageTransport::Spi && profile.storage.powerPin != kPinUnused) {
    papyrix::sd::prepareSdForDisplayProbe(profile.storage.powerPin);
  } else if (profile.storage.powerPin != kPinUnused) {
    pinMode(profile.storage.powerPin, OUTPUT);
    digitalWrite(profile.storage.powerPin, profile.storage.powerActiveHigh ? HIGH : LOW);
  }
}

uint64_t powerButtonWakeMask(const BoardProfile& profile) {
  if (profile.input.power == kPinUnused || profile.input.power >= 64) return 0;
  return 1ULL << profile.input.power;
}

}  // namespace papyrix::board
