#pragma once

#include <cstddef>
#include <cstdint>

namespace papyrix::eink {

enum class Uc8279RefreshMode : uint8_t { Full, Half, Fast };

class Uc8279Bus {
 public:
  virtual ~Uc8279Bus() = default;
  virtual void reset(uint16_t extraSettleMs) = 0;
  virtual void command(uint8_t command) = 0;
  virtual void data(uint8_t value) = 0;
  virtual void data(const uint8_t* values, size_t size) = 0;
  virtual void commandData(uint8_t command, const uint8_t* values, size_t size) = 0;
  virtual void beginData(uint8_t command) = 0;
  virtual void write(const uint8_t* values, size_t size) = 0;
  virtual void endData() = 0;
  virtual bool waitBusy(const char* operation, bool requireAssertion = false) = 0;
  virtual bool waitReady(const char* operation) = 0;
};

class Uc8279X3Driver {
 public:
  static constexpr uint16_t WIDTH = 792;
  static constexpr uint16_t HEIGHT = 528;
  static constexpr uint16_t WIDTH_BYTES = 99;
  static constexpr uint32_t BUFFER_SIZE = 52272;

  static_assert(static_cast<uint32_t>(WIDTH_BYTES) * HEIGHT == BUFFER_SIZE);

  void begin(Uc8279Bus& bus);
  bool display(Uc8279Bus& bus, const uint8_t* frame, Uc8279RefreshMode mode, bool turnOff);
  void requestResync();
  void setBackgroundHint(bool dark) { darkBackground_ = dark; }
  void copyGrayscaleLsb(Uc8279Bus& bus, const uint8_t* plane);
  void copyGrayscaleMsb(Uc8279Bus& bus, const uint8_t* plane);
  void displayGray(Uc8279Bus& bus, bool turnOff);
  void cleanupGrayscale(Uc8279Bus& bus, const uint8_t* bwFrame);
  void grayscaleRevert(Uc8279Bus& bus);
  bool deepSleep(Uc8279Bus& bus);

 private:
  void sendScript(Uc8279Bus& bus, const uint8_t* script, size_t size);
  void loadBank(Uc8279Bus& bus, const uint8_t (*bank)[43]);
  void sendPlaneFlipped(Uc8279Bus& bus, uint8_t command, const uint8_t* plane);
  void sendPlaneFlippedInverted(Uc8279Bus& bus, uint8_t command, const uint8_t* plane);
  void fillPlane(Uc8279Bus& bus, uint8_t command, uint8_t value);
  void fullWindowIn(Uc8279Bus& bus);
  void loadGrayscaleBank(Uc8279Bus& bus);
  bool triggerGrayscaleRefresh(Uc8279Bus& bus, bool turnOff);
  void invalidateAfterTimeout();
  void recoverAfterPowerOffTimeout();

  bool screenOn_ = false;
  bool darkBackground_ = false;
  bool firstRefresh_ = true;
  bool oldPlaneValid_ = false;
  bool forceFullSync_ = false;
  uint8_t initialFullsRemaining_ = 0;
  bool grayscaleMode_ = false;
  bool lsbValid_ = false;
  bool msbValid_ = false;
};

Uc8279X3Driver& uc8279X3Driver();

}  // namespace papyrix::eink
