#include "StickyDisplayProbe.h"

#if PAPYRIX_TARGET_STICKY
#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>

namespace papyrix::eink {

// Adapted from FreeInk 683fe03009fdd03c725015d167da5362b01911c1. See FREEINK_LICENSE.
StickyProbeResult probeStickyDisplayController(const board::BoardProfile& profile) {
  const auto& d = profile.display;
  const bool restoreSpi = SPI.bus() != nullptr;
  const int select = restoreSpi ? SPI.pinSS() : d.cs;
  if (restoreSpi) SPI.end();
  for (const int8_t pin : {d.cs, d.dc, d.sclk, d.mosi, d.rst, d.busy, d.power, profile.storage.spiCs}) {
    if (pin >= 0) gpio_hold_dis(static_cast<gpio_num_t>(pin));
  }
  pinMode(profile.storage.spiCs, OUTPUT);
  digitalWrite(profile.storage.spiCs, HIGH);
  pinMode(d.power, OUTPUT);
  digitalWrite(d.power, HIGH);
  delay(50);
  pinMode(d.cs, OUTPUT);
  digitalWrite(d.cs, HIGH);
  pinMode(d.dc, OUTPUT);
  digitalWrite(d.dc, HIGH);
  pinMode(d.sclk, OUTPUT);
  digitalWrite(d.sclk, LOW);
  pinMode(d.mosi, OUTPUT);
  pinMode(d.busy, INPUT_PULLUP);
  pinMode(d.rst, OUTPUT);
  digitalWrite(d.rst, LOW);
  delay(20);
  digitalWrite(d.rst, HIGH);
  delay(10);
  const auto clock = [&] {
    digitalWrite(d.sclk, HIGH);
    delayMicroseconds(5);
    digitalWrite(d.sclk, LOW);
    delayMicroseconds(5);
  };
  digitalWrite(d.cs, LOW);
  digitalWrite(d.dc, LOW);
  delayMicroseconds(5);
  for (uint8_t command = 0x70, bit = 0; bit < 8; ++bit, command <<= 1) {
    digitalWrite(d.mosi, (command & 0x80) ? HIGH : LOW);
    clock();
  }
  pinMode(d.mosi, INPUT_PULLUP);
  digitalWrite(d.dc, HIGH);
  delayMicroseconds(5);
  uint8_t revision = 0;
  for (unsigned bit = 0; bit < 8; ++bit) {
    revision = static_cast<uint8_t>((revision << 1) | (digitalRead(d.mosi) == HIGH));
    clock();
  }
  digitalWrite(d.cs, HIGH);
  pinMode(d.mosi, OUTPUT);
  delay(30);
  const bool busyHigh = digitalRead(d.busy) == HIGH;
  bool stable = true;
  for (unsigned sample = 0; sample < 2; ++sample) {
    delay(1);
    stable &= (digitalRead(d.busy) == HIGH) == busyHigh;
  }
  if (restoreSpi) SPI.begin(d.sclk, profile.storage.spiMiso, d.mosi, select);
  return {stable, revision, busyHigh};
}
}  // namespace papyrix::eink
#endif
