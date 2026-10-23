#pragma once

#include <SPI.h>

#include <cstdint>

#include "Uc8279X3Driver.h"

namespace papyrix::eink {

class Uc8279SpiBus : public Uc8279Bus {
 public:
  Uc8279SpiBus(int8_t cs, int8_t dc, int8_t reset, int8_t busy, const SPISettings& settings, uint16_t resetLowMs = 10);

  void reset(uint16_t extraSettleMs) override;
  void command(uint8_t command) override;
  void data(uint8_t value) override;
  void data(const uint8_t* values, size_t size) override;
  void commandData(uint8_t command, const uint8_t* values, size_t size) override;
  void beginData(uint8_t command) override;
  void write(const uint8_t* values, size_t size) override;
  void endData() override;
  bool waitBusy(const char* operation, bool requireAssertion = false) override;
  bool waitReady(const char* operation) override;

 private:
  int8_t cs_;
  int8_t dc_;
  int8_t reset_;
  int8_t busy_;
  SPISettings settings_;
  uint16_t resetLowMs_;
};

}  // namespace papyrix::eink
