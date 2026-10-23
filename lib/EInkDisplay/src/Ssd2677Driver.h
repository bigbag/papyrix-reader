#pragma once

#include "Uc8279X3Driver.h"

namespace papyrix::eink {
enum class Ssd2677RefreshMode : uint8_t { Full, Fast };
enum class Ssd2677InitResult : uint8_t { Ok, OutOfMemory, PanelError };

class Ssd2677Driver {
 public:
  Ssd2677Driver() = default;
  ~Ssd2677Driver();
  Ssd2677Driver(const Ssd2677Driver&) = delete;
  Ssd2677Driver& operator=(const Ssd2677Driver&) = delete;
  Ssd2677InitResult begin(Uc8279Bus& bus);
  bool display(Uc8279Bus& bus, const uint8_t* frame, Ssd2677RefreshMode mode, bool turnOff);
  void requestResync();
  bool copyGrayscaleLsb(Uc8279Bus& bus, const uint8_t* plane, bool absolute);
  bool copyGrayscaleMsb(Uc8279Bus& bus, const uint8_t* plane);
  bool displayGray(Uc8279Bus& bus, bool turnOff);
  bool cleanupGrayscale(Uc8279Bus& bus, const uint8_t* bwFrame);
  bool deepSleep(Uc8279Bus& bus);

 private:
  bool initPanel(Uc8279Bus& bus);
  bool latchWaveform(Uc8279Bus& bus, uint8_t code);
  void sendPacked(Uc8279Bus& bus, const uint8_t* high, const uint8_t* low);
  bool refresh(Uc8279Bus& bus, bool turnOff);
  bool powerOff(Uc8279Bus& bus);
  bool fail();
  uint8_t* old_ = nullptr;
  uint8_t* lsb_ = nullptr;
  uint8_t waveform_ = 0;
  bool initialized_ = false;
  bool baselineValid_ = false;
  bool needsPowerOn_ = true;
  bool absolute_ = false;
  bool lsbValid_ = false;
  bool msbValid_ = false;
  bool graySucceeded_ = false;
};
Ssd2677Driver& ssd2677Driver();
}  // namespace papyrix::eink
