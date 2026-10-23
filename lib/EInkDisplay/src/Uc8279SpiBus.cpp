#include "Uc8279SpiBus.h"

#include <Arduino.h>
#include <Logging.h>
#include <driver/gpio.h>

#define TAG "UC8279_BUS"

namespace papyrix::eink {

Uc8279SpiBus::Uc8279SpiBus(int8_t cs, int8_t dc, int8_t reset, int8_t busy, const SPISettings& settings,
                           uint16_t resetLowMs)
    : cs_(cs), dc_(dc), reset_(reset), busy_(busy), settings_(settings), resetLowMs_(resetLowMs) {}

void Uc8279SpiBus::reset(uint16_t extraSettleMs) {
  gpio_hold_dis(static_cast<gpio_num_t>(reset_));
  pinMode(reset_, OUTPUT);
  digitalWrite(reset_, HIGH);
  delay(10);
  digitalWrite(reset_, LOW);
  delay(resetLowMs_);
  digitalWrite(reset_, HIGH);
  delay(10);
  if (extraSettleMs > 0) delay(extraSettleMs);
}

void Uc8279SpiBus::command(uint8_t commandValue) {
  SPI.beginTransaction(settings_);
  digitalWrite(dc_, LOW);
  digitalWrite(cs_, LOW);
  SPI.transfer(commandValue);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

void Uc8279SpiBus::data(uint8_t value) {
  SPI.beginTransaction(settings_);
  digitalWrite(dc_, HIGH);
  digitalWrite(cs_, LOW);
  SPI.transfer(value);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

void Uc8279SpiBus::data(const uint8_t* values, size_t size) {
  SPI.beginTransaction(settings_);
  digitalWrite(dc_, HIGH);
  digitalWrite(cs_, LOW);
  SPI.writeBytes(values, size);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

void Uc8279SpiBus::commandData(uint8_t commandValue, const uint8_t* values, size_t size) {
  SPI.beginTransaction(settings_);
  digitalWrite(cs_, LOW);
  digitalWrite(dc_, LOW);
  SPI.transfer(commandValue);
  digitalWrite(dc_, HIGH);
  SPI.writeBytes(values, size);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

void Uc8279SpiBus::beginData(uint8_t commandValue) {
  command(commandValue);
  SPI.beginTransaction(settings_);
  digitalWrite(dc_, HIGH);
  digitalWrite(cs_, LOW);
}

void Uc8279SpiBus::write(const uint8_t* values, size_t size) { SPI.writeBytes(values, size); }

void Uc8279SpiBus::endData() {
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

bool Uc8279SpiBus::waitBusy(const char* operation, bool requireAssertion) {
  const unsigned long assertionStart = millis();
  while (digitalRead(busy_) == HIGH && millis() - assertionStart <= 1000) delay(1);
  if (digitalRead(busy_) != LOW) {
    if (requireAssertion) {
      LOG_ERR(TAG, "BUSY did not assert for %s", operation == nullptr ? "operation" : operation);
    } else {
      LOG_DBG(TAG, "BUSY did not assert for %s", operation == nullptr ? "operation" : operation);
    }
    return !requireAssertion;
  }
  return waitReady(operation);
}

bool Uc8279SpiBus::waitReady(const char* operation) {
  const unsigned long completionStart = millis();
  while (digitalRead(busy_) == LOW && millis() - completionStart <= 30000) delay(1);
  if (digitalRead(busy_) != HIGH) {
    LOG_ERR(TAG, "BUSY did not complete for %s", operation == nullptr ? "operation" : operation);
    return false;
  }
  return true;
}

}  // namespace papyrix::eink
