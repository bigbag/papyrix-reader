#include "PowerPolicy.h"

#include <Arduino.h>
#include <SDPowerControl.h>
#include <SPI.h>
#include <driver/gpio.h>

#include "TargetConfig.h"

namespace papyrix::board {

void releaseDeepSleepHolds() { gpio_deep_sleep_hold_dis(); }

static void prepareSleepPins(const BoardProfile& profile, bool externalPower, bool clockStandby) {
  if (profile.family == McuFamily::Esp32S3) {
    const auto holdOutput = [](int8_t pin, bool high) {
      if (pin == kPinUnused) return;
      gpio_hold_dis(static_cast<gpio_num_t>(pin));
      pinMode(pin, OUTPUT);
      digitalWrite(pin, high ? HIGH : LOW);
      gpio_hold_en(static_cast<gpio_num_t>(pin));
    };
    if (profile.touch.rst != profile.display.rst) holdOutput(profile.touch.rst, false);
    holdOutput(profile.touch.powerPin, !profile.touch.powerActiveHigh);
    if (profile.storage.transport == StorageTransport::Spi) {
      papyrix::sd::disableSdPower(profile.storage.powerPin, profile.storage.spiCs, profile.display.sclk,
                                  profile.storage.spiMiso, profile.display.mosi);
    } else {
      for (const int8_t pin : {profile.storage.sdmmcClk, profile.storage.sdmmcCmd, profile.storage.sdmmcDat0,
                               profile.storage.sdmmcDat1, profile.storage.sdmmcDat2, profile.storage.sdmmcDat3}) {
        if (pin == kPinUnused) continue;
        gpio_hold_dis(static_cast<gpio_num_t>(pin));
        pinMode(pin, INPUT);
      }
    }
    holdOutput(profile.storage.powerPin, !profile.storage.powerActiveHigh);
    if (clockStandby && profile.display.power != kPinUnused) {
      for (const int8_t pin : {profile.display.sclk, profile.display.mosi, profile.display.cs, profile.display.dc,
                               profile.display.rst, profile.display.busy}) {
        if (pin == kPinUnused) continue;
        gpio_hold_dis(static_cast<gpio_num_t>(pin));
        pinMode(pin, INPUT);
        gpio_hold_en(static_cast<gpio_num_t>(pin));
      }
    } else {
      holdOutput(profile.display.rst, true);
    }
    holdOutput(profile.power.latchPin, profile.power.latchActiveHigh);
    holdOutput(profile.power.latchPin2, profile.power.latchActiveHigh);
    holdOutput(profile.power.chargeEnablePin, profile.power.chargeEnableActiveHigh);
    holdOutput(profile.power.microphonePowerPin, false);
    holdOutput(profile.display.power, false);
    holdOutput(profile.frontLight.gpio, !profile.frontLight.activeHigh);
    holdOutput(profile.frontLight.warmGpio, !profile.frontLight.activeHigh);
    return;
  }
  if (profile.storage.transport == StorageTransport::Spi && profile.storage.powerPin != kPinUnused) {
    if (clockStandby && profile.id == BoardId::X3) {
      // X3 GPIO13 must stay HIGH for MCU timer wake.
      pinMode(profile.storage.powerPin, OUTPUT);
      digitalWrite(profile.storage.powerPin, HIGH);
      gpio_hold_dis(static_cast<gpio_num_t>(profile.storage.powerPin));
    } else {
      papyrix::sd::disableSdPower(profile.storage.powerPin, profile.storage.spiCs, profile.display.sclk,
                                  profile.storage.spiMiso, profile.display.mosi);
    }
    gpio_hold_en(static_cast<gpio_num_t>(profile.storage.powerPin));
  }
  if (profile.power.latchPin != kPinUnused) {
    const auto latch = static_cast<gpio_num_t>(profile.power.latchPin);
    gpio_hold_dis(latch);
    pinMode(profile.power.latchPin, OUTPUT);
    const bool keepPowered =
        externalPower || clockStandby ? profile.power.latchActiveHigh : !profile.power.latchActiveHigh;
    digitalWrite(profile.power.latchPin, keepPowered ? HIGH : LOW);
    gpio_hold_en(latch);
  }

  if (profile.id != BoardId::X3 && !clockStandby) return;
  gpio_hold_dis(static_cast<gpio_num_t>(profile.display.rst));
  pinMode(profile.display.rst, OUTPUT);
  digitalWrite(profile.display.rst, HIGH);
  gpio_hold_en(static_cast<gpio_num_t>(profile.display.rst));
}

void prepareDeepSleepPins(const BoardProfile& profile, bool externalPower) {
  prepareSleepPins(profile, externalPower, false);
}

void prepareClockSleepPins(const BoardProfile& profile) { prepareSleepPins(profile, false, true); }

void shutdownRecoveryRails(const BoardProfile& profile) {
  if (profile.display.power != kPinUnused) {
    pinMode(profile.display.power, OUTPUT);
    digitalWrite(profile.display.power, LOW);
  }
  if (profile.storage.transport == StorageTransport::Spi && profile.storage.powerPin != kPinUnused) {
    papyrix::sd::disableSdPower(profile.storage.powerPin, profile.storage.spiCs, profile.display.sclk,
                                profile.storage.spiMiso, profile.display.mosi);
  } else if (profile.storage.powerPin != kPinUnused) {
    for (const int pin : {profile.storage.sdmmcClk, profile.storage.sdmmcCmd, profile.storage.sdmmcDat0,
                          profile.storage.sdmmcDat1, profile.storage.sdmmcDat2, profile.storage.sdmmcDat3}) {
      if (pin == kPinUnused) continue;
      pinMode(pin, INPUT);
    }
    pinMode(profile.storage.powerPin, OUTPUT);
    digitalWrite(profile.storage.powerPin, profile.storage.powerActiveHigh ? LOW : HIGH);
  }
}

void restoreRecoveryStorage(const BoardProfile& profile) {
  if (profile.storage.transport == StorageTransport::Spi) {
    papyrix::sd::prepareSdForDisplayProbe(profile.storage.powerPin, profile.storage.spiCs);
    if (profile.display.power != kPinUnused) {
      pinMode(profile.display.power, OUTPUT);
      digitalWrite(profile.display.power, HIGH);
      gpio_hold_dis(static_cast<gpio_num_t>(profile.display.power));
      delay(50);
    }
    pinMode(profile.display.cs, OUTPUT);
    digitalWrite(profile.display.cs, HIGH);
    gpio_hold_dis(static_cast<gpio_num_t>(profile.display.cs));
    for (const int8_t pin : {profile.display.sclk, profile.display.mosi, profile.storage.spiMiso}) {
      if (pin != kPinUnused) gpio_hold_dis(static_cast<gpio_num_t>(pin));
    }
    SPI.begin(profile.display.sclk, profile.storage.spiMiso, profile.display.mosi, profile.storage.spiCs);
  } else if (profile.storage.powerPin != kPinUnused) {
    gpio_hold_dis(static_cast<gpio_num_t>(profile.storage.powerPin));
    pinMode(profile.storage.powerPin, OUTPUT);
    digitalWrite(profile.storage.powerPin, profile.storage.powerActiveHigh ? HIGH : LOW);
  }
}

uint64_t powerButtonWakeMask(const BoardProfile& profile) {
  if (profile.input.power == kPinUnused || profile.input.power >= 64) return 0;
  return 1ULL << profile.input.power;
}

}  // namespace papyrix::board
