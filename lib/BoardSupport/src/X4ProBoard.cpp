#include "X4ProBoard.h"

#include <Arduino.h>
#include <TargetConfig.h>
#include <driver/gpio.h>

namespace papyrix::board::x4pro {

#if PAPYRIX_TARGET_X4PRO
namespace {

void setOutput(int8_t pin, bool high) {
  if (pin == kPinUnused) return;
  gpio_hold_dis(static_cast<gpio_num_t>(pin));
  pinMode(pin, OUTPUT);
  digitalWrite(pin, high ? HIGH : LOW);
}

void setInput(int8_t pin) {
  if (pin == kPinUnused) return;
  gpio_hold_dis(static_cast<gpio_num_t>(pin));
  pinMode(pin, INPUT);
}

}  // namespace

void prepareCharacterization(const BoardProfile& profile) {
  setInput(profile.display.sclk);
  setInput(profile.display.mosi);
  setInput(profile.display.cs);
  setInput(profile.display.dc);
  setInput(profile.display.rst);
  setInput(profile.display.busy);
  setOutput(profile.storage.powerPin, !profile.storage.powerActiveHigh);
  setOutput(profile.frontLight.gpio, !profile.frontLight.activeHigh);
  setOutput(profile.frontLight.warmGpio, !profile.frontLight.activeHigh);
}

#endif

}  // namespace papyrix::board::x4pro
