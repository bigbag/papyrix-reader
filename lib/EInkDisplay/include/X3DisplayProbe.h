#pragma once

#include <cstddef>
#include <cstdint>

namespace papyrix::eink {

constexpr size_t X3_DISPLAY_MTP_SIZE = 48;

enum class X3DisplayVerdict : uint8_t { UC8253StableDefault, UC8279Confirmed, Inconclusive };
enum class X4ProPanelVariant : uint8_t { Ssd1677, Uc8179, Uc8279 };

struct X3DisplayProbeSample {
  uint8_t ver[5] = {};
  uint8_t flg = 0;
};

struct X3DisplayProbeReport {
  X3DisplayProbeSample pass1{};
  X3DisplayProbeSample pass2{};
  bool mtpValid = false;
  uint8_t mtp[X3_DISPLAY_MTP_SIZE] = {};
  bool mtpRepeatable = false;
  X3DisplayVerdict verdict = X3DisplayVerdict::Inconclusive;
};

struct X3StockProbeResult {
  uint8_t ver[3] = {};
  bool busyTimedOut = false;
  X3DisplayVerdict verdict = X3DisplayVerdict::Inconclusive;
};

struct X3DisplayProbePins {
  int8_t sclk;
  int8_t sda;
  int8_t cs;
  int8_t dc;
  int8_t reset;
  int8_t busy;
};

class X3DisplayProbeTransport {
 public:
  virtual ~X3DisplayProbeTransport() = default;
  virtual void readPass(uint8_t resetLowMs, X3DisplayProbeSample& sample) = 0;
  virtual bool readMtp(uint8_t* output, size_t size) = 0;
  virtual void pause(uint16_t milliseconds) = 0;
  virtual void releasePins() = 0;
};

X3DisplayVerdict classifyX3Display(const X3DisplayProbeReport& report);
X4ProPanelVariant classifyX4ProPanel(const X3DisplayProbeReport& report);
X3DisplayProbeReport runX3DisplayProbe(X3DisplayProbeTransport& transport);
X3StockProbeResult probeX3DisplayController(const X3DisplayProbePins& pins);
X3DisplayProbeReport probeX4ProDisplayController(const X3DisplayProbePins& pins);

}  // namespace papyrix::eink
