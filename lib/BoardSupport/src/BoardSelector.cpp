#include "BoardSelector.h"

#include "HardwareIdentity.h"
#include "HardwareRecovery.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <Logging.h>
#include <Preferences.h>
#include <Wire.h>
#include <X3DisplayProbe.h>
#if PAPYRIX_TARGET_X4CLASSIC
#include <X4ClassicDisplayProbe.h>
#include <X4ClassicPanelPolicy.h>
#endif
#if PAPYRIX_TARGET_STICKY
#include <StickyDisplayProbe.h>
#endif

#include <cstdio>

#include "X3ControllerPolicy.h"
#endif

namespace papyrix::board {

namespace {

bool decodeStoredBoard(uint8_t value, BoardId& board) {
  if (value == kStoredX4) {
    board = BoardId::X4;
    return true;
  }
  if (value == kStoredX3) {
    board = BoardId::X3;
    return true;
  }
  return false;
}

}  // namespace

BoardProbeVerdict classifyBoardProbe(const BoardProbeReport& report) {
  if (report.pass1.score >= 2 && report.pass2.score >= 2) return BoardProbeVerdict::X3;
  if (report.pass1.score == 0 && report.pass2.score == 0) return BoardProbeVerdict::X4;
  return BoardProbeVerdict::Inconclusive;
}

BoardSelection resolveBoardSelection(uint8_t overrideValue, uint8_t cacheValue, BoardProbeVerdict liveVerdict) {
  BoardSelection decision{};

  if (overrideValue != kStoredAuto) {
    if (decodeStoredBoard(overrideValue, decision.board)) {
      decision.source = BoardSelectionSource::Override;
      return decision;
    }
    decision.invalidOverride = true;
  }

  if (cacheValue != kStoredAuto) {
    if (decodeStoredBoard(cacheValue, decision.board)) {
      decision.source = BoardSelectionSource::Cache;
      return decision;
    }
    decision.invalidCache = true;
  }

  if (liveVerdict == BoardProbeVerdict::X3) {
    decision.board = BoardId::X3;
    decision.source = BoardSelectionSource::Probe;
    decision.writeCache = true;
    decision.storedValue = kStoredX3;
    return decision;
  }
  if (liveVerdict == BoardProbeVerdict::X4) {
    decision.board = BoardId::X4;
    decision.source = BoardSelectionSource::Probe;
    decision.writeCache = true;
    decision.storedValue = kStoredX4;
    return decision;
  }

  decision.board = BoardId::X4;
  decision.source = BoardSelectionSource::Fallback;
  return decision;
}

const char* boardSelectionSourceName(BoardSelectionSource source) {
  switch (source) {
    case BoardSelectionSource::Fixed:
      return "fixed";
    case BoardSelectionSource::Override:
      return "override";
    case BoardSelectionSource::Cache:
      return "cache";
    case BoardSelectionSource::Probe:
      return "probe";
    case BoardSelectionSource::Fallback:
      return "fallback";
  }
  return "unknown";
}

#ifdef ARDUINO
namespace {

constexpr uint16_t kI2cTimeoutMs = 6;
constexpr uint16_t kInterPassDelayMs = 2;
constexpr uint8_t kBq27220SocRegister = 0x2C;
constexpr uint8_t kBq27220VoltageRegister = 0x08;
constexpr uint8_t kDs3231SecondsRegister = 0x00;
constexpr uint8_t kQmi8658Address = 0x6B;
constexpr uint8_t kQmi8658AlternateAddress = 0x6A;
constexpr uint8_t kQmi8658WhoAmIRegister = 0x00;
constexpr uint8_t kQmi8658WhoAmI = 0x05;

bool readI2c8(uint8_t address, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<int>(address), 1) != 1 || !Wire.available()) return false;
  value = Wire.read();
  return true;
}

bool readI2c16Le(uint8_t address, uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<int>(address), 2) != 2 || Wire.available() < 2) return false;
  const uint8_t low = Wire.read();
  const uint8_t high = Wire.read();
  value = (static_cast<uint16_t>(high) << 8) | low;
  return true;
}

bool probeBq27220(const BoardProfile& profile) {
  uint16_t stateOfCharge = 0;
  if (!readI2c16Le(profile.battery.i2cAddress, kBq27220SocRegister, stateOfCharge) || stateOfCharge > 100) {
    return false;
  }
  uint16_t voltageMv = 0;
  return readI2c16Le(profile.battery.i2cAddress, kBq27220VoltageRegister, voltageMv) && voltageMv >= 2500 &&
         voltageMv <= 5000;
}

bool probeDs3231(const BoardProfile& profile) {
  uint8_t seconds = 0;
  if (!readI2c8(profile.rtc.i2cAddress, kDs3231SecondsRegister, seconds)) return false;
  return ((seconds >> 4) & 0x07) <= 5 && (seconds & 0x0F) <= 9;
}

bool probeQmi8658() {
  uint8_t whoAmI = 0;
  return (readI2c8(kQmi8658Address, kQmi8658WhoAmIRegister, whoAmI) && whoAmI == kQmi8658WhoAmI) ||
         (readI2c8(kQmi8658AlternateAddress, kQmi8658WhoAmIRegister, whoAmI) && whoAmI == kQmi8658WhoAmI);
}

BoardProbePass runProbePass(const BoardProfile& x3) {
  BoardProbePass pass{};
  pass.bq27220 = probeBq27220(x3);
  pass.ds3231 = probeDs3231(x3);
  pass.qmi8658 = probeQmi8658();
  pass.score = static_cast<uint8_t>(pass.bq27220 + pass.ds3231 + pass.qmi8658);
  return pass;
}

BoardProbeReport runC3Probe(const BoardProfile& x3) {
  Wire.begin(x3.battery.sda, x3.battery.scl, x3.rtc.i2cHz);
  Wire.setTimeOut(kI2cTimeoutMs);
  BoardProbeReport report{};
  report.pass1 = runProbePass(x3);
  delay(kInterPassDelayMs);
  report.pass2 = runProbePass(x3);
  LOG_INF("BOARD", "probe: pass1=%u(bq=%d rtc=%d imu=%d) pass2=%u(bq=%d rtc=%d imu=%d)", report.pass1.score,
          report.pass1.bq27220, report.pass1.ds3231, report.pass1.qmi8658, report.pass2.score, report.pass2.bq27220,
          report.pass2.ds3231, report.pass2.qmi8658);
  Wire.end();
  pinMode(x3.battery.sda, INPUT);
  pinMode(x3.battery.scl, INPUT);
  return report;
}

uint8_t readHardwareByte(const char* key) {
  Preferences preferences;
  if (!preferences.begin(kHardwareNamespace, true)) return kStoredAuto;
  const uint8_t value = preferences.getUChar(key, kStoredAuto);
  preferences.end();
  return value;
}

bool writeHardwareByte(const char* key, uint8_t value) {
  Preferences preferences;
  if (!preferences.begin(kHardwareNamespace, false)) return false;
  const uint8_t existing = preferences.getUChar(key, kStoredAuto);
  const bool stored = existing == value || preferences.putUChar(key, value) == sizeof(value);
  preferences.end();
  return stored;
}

bool writeControllerCache(uint8_t value) {
  if (value != kStoredUc8253 && value != kStoredUc8279X3) return false;
  Preferences preferences;
  if (!preferences.begin(kHardwareNamespace, false)) return false;
  const uint8_t existingValue = preferences.getUChar(kControllerCacheKey, kStoredControllerAuto);
  const uint8_t existingVersion = preferences.getUChar(kControllerVersionKey, kStoredControllerAuto);
  bool stored = existingValue == value;
  if (!stored) stored = preferences.putUChar(kControllerCacheKey, value) == sizeof(value);
  if (stored && existingVersion != kControllerCacheVersion) {
    stored = preferences.putUChar(kControllerVersionKey, kControllerCacheVersion) == sizeof(kControllerCacheVersion);
  }
  preferences.end();
  return stored;
}

PanelSelectionSource panelSource(ControllerSelectionSource source) {
  using Source = ControllerSelectionSource;
  switch (source) {
    case Source::X4Default:
      return PanelSelectionSource::X4Default;
    case Source::Override:
      return PanelSelectionSource::Override;
    case Source::Cache:
      return PanelSelectionSource::Cache;
    case Source::Probe:
      return PanelSelectionSource::Probe;
    case Source::Fallback:
      return PanelSelectionSource::Fallback;
  }
  return PanelSelectionSource::Fallback;
}

const char* probeVerdictName(eink::X3DisplayVerdict verdict) {
  switch (verdict) {
    case eink::X3DisplayVerdict::UC8253StableDefault:
      return "uc8253-stable-default";
    case eink::X3DisplayVerdict::UC8279Confirmed:
      return "uc8279-confirmed";
    case eink::X3DisplayVerdict::Inconclusive:
      return "inconclusive";
  }
  return "unknown";
}

void logDisplayProbe(const eink::X3DisplayProbeReport& report) {
  LOG_INF("BOARD", "display probe: VER1=%02X %02X %02X %02X %02X FLG1=%02X", report.pass1.ver[0], report.pass1.ver[1],
          report.pass1.ver[2], report.pass1.ver[3], report.pass1.ver[4], report.pass1.flg);
  LOG_INF("BOARD", "display probe: VER2=%02X %02X %02X %02X %02X FLG2=%02X", report.pass2.ver[0], report.pass2.ver[1],
          report.pass2.ver[2], report.pass2.ver[3], report.pass2.ver[4], report.pass2.flg);
  if (!report.mtpValid) return;
  char line[145] = {};
  size_t used = 0;
  for (size_t index = 0; index < sizeof(report.mtp); ++index) {
    const size_t remaining = sizeof(line) - used;
    const int written =
        snprintf(line + used, remaining, index == 0 ? "%02X" : " %02X", static_cast<unsigned int>(report.mtp[index]));
    if (written <= 0 || static_cast<size_t>(written) >= remaining) break;
    used += static_cast<size_t>(written);
  }
  LOG_INF("BOARD", "display probe MTP: %s%s", line, report.mtpRepeatable ? " (repeat matched)" : "");
}

}  // namespace

void selectBoard(HardwareIdentity& identity) {
#if PAPYRIX_TARGET_XTEINK_C3
  const uint8_t overrideValue = readHardwareByte(kBoardOverrideKey);
  const uint8_t cacheValue = readHardwareByte(kBoardCacheKey);
  BoardProbeReport report{};
  BoardSelection selection = resolveBoardSelection(overrideValue, cacheValue, BoardProbeVerdict::Inconclusive);
  if (selection.source == BoardSelectionSource::Fallback) {
    const BoardProfile* x3 = findProfile(BoardId::X3);
    if (x3 != nullptr) report = runC3Probe(*x3);
    selection = resolveBoardSelection(overrideValue, cacheValue, classifyBoardProbe(report));
  }
  if (selection.invalidOverride) LOG_WRN("BOARD", "invalid board override: %u", overrideValue);
  if (selection.invalidCache) LOG_WRN("BOARD", "invalid board cache: %u", cacheValue);
  identity.applyBoardSelection(selection, report);
  const bool cached = selection.writeCache && writeHardwareByte(kBoardCacheKey, selection.storedValue);
  LOG_INF("BOARD", "board: %s %ux%u (%s%s)", identity.profile().name,
          static_cast<unsigned int>(identity.profile().display.width),
          static_cast<unsigned int>(identity.profile().display.height), boardSelectionSourceName(selection.source),
          cached                 ? ", cached"
          : selection.writeCache ? ", cache write failed"
                                 : "");
#else
  BoardSelection selection{};
  selection.board = bootProfile().id;
  selection.source = BoardSelectionSource::Fixed;
  identity.applyBoardSelection(selection, {});
#endif
}

void selectPanel(HardwareIdentity& identity) {
#if PAPYRIX_TARGET_XTEINK_C3
  if (identity.board() == BoardId::X4) {
    identity.setPanel(eink::DisplayController::SSD1677, PanelSelectionSource::X4Default);
    LOG_INF("BOARD", "panel: SSD1677 (x4-default)");
    return;
  }

  const uint8_t overrideValue = readHardwareByte(kControllerOverrideKey);
  const uint8_t cacheValue = readHardwareByte(kControllerCacheKey);
  const uint8_t cacheVersion = readHardwareByte(kControllerVersionKey);
  ControllerDecision decision = resolveStoredX3Controller(overrideValue, cacheValue, cacheVersion);
  if (decision.invalidOverride) LOG_WRN("BOARD", "invalid panel override: %u", overrideValue);
  if (decision.invalidCache) {
    LOG_WRN("BOARD", "invalid panel cache: value=%u version=%u expected=%u", cacheValue, cacheVersion,
            kControllerCacheVersion);
  }
  if (decision.resolved) {
    identity.setPanel(decision.controller, panelSource(decision.source));
    LOG_INF("BOARD", "panel: %s (%s)", eink::displayControllerName(identity.panel()),
            panelSelectionSourceName(identity.panelSource()));
    return;
  }

  const auto& display = identity.profile().display;
  const eink::X3DisplayProbePins pins{display.sclk, display.mosi, display.cs, display.dc, display.rst, display.busy};
  const eink::X3DisplayProbeReport report = eink::probeX3DisplayController(pins);
  logDisplayProbe(report);
  LOG_INF("BOARD", "display probe verdict: %s", probeVerdictName(report.verdict));
  decision = resolveProbedX3Controller(report.verdict);
  identity.setPanel(decision.controller, panelSource(decision.source));
  const bool cached = decision.writeCache && writeControllerCache(decision.storedValue);
  LOG_INF("BOARD", "panel: %s (%s%s)", eink::displayControllerName(identity.panel()),
          panelSelectionSourceName(identity.panelSource()),
          cached                ? ", cached"
          : decision.writeCache ? ", cache write failed"
                                : ", not cached");
#elif PAPYRIX_TARGET_X4PRO
  const auto& display = identity.profile().display;
  const eink::X3DisplayProbePins pins{display.sclk, display.mosi, display.cs, display.dc, display.rst, display.busy};
  const eink::X3DisplayProbeReport report = eink::probeX3DisplayController(pins);
  logDisplayProbe(report);
  const eink::X4ProPanelVariant variant = eink::classifyX4ProPanel(report);
  if (variant == eink::X4ProPanelVariant::Uc8279) {
    identity.setPanel(eink::DisplayController::UC8279_X4PRO, PanelSelectionSource::Probe, report.pass2.ver[2]);
  } else if (variant == eink::X4ProPanelVariant::Uc8179) {
    identity.setPanel(eink::DisplayController::UC8179_X4PRO, PanelSelectionSource::Probe, report.pass2.ver[2]);
  } else {
    identity.setPanel(eink::DisplayController::SSD1677, PanelSelectionSource::Fallback);
  }
  LOG_INF("BOARD", "panel: %s (%s, LUT=%02X)", eink::displayControllerName(identity.panel()),
          panelSelectionSourceName(identity.panelSource()), identity.panelVariant());
#elif PAPYRIX_TARGET_X4CLASSIC
  identity.clearPanelSelection();
  Preferences factory;
  const bool opened = factory.begin("hw_calib", true);
  const uint8_t screenType = opened ? factory.getUChar("screenType", 0) : 0;
  if (opened) factory.end();
  auto decision = resolveClassicPanel(opened, screenType, false, 0);
  if (!decision.resolved || decision.controller == eink::DisplayController::UC8279_X4PRO) {
    const auto& display = identity.profile().display;
    const eink::X3DisplayProbePins pins{display.sclk, display.mosi, display.cs, display.dc, display.rst, display.busy};
    const auto probe = eink::probeX4ClassicDisplayController(pins);
    decision = resolveClassicPanel(opened, screenType, probe.valid, probe.id);
  }
  if (decision.resolved) {
    identity.setPanel(
        decision.controller,
        decision.source == ClassicPanelSource::Factory ? PanelSelectionSource::Factory : PanelSelectionSource::Probe,
        decision.variant);
    LOG_INF("BOARD", "panel: %s (%s, LUT=%02X)", eink::displayControllerName(identity.panel()),
            panelSelectionSourceName(identity.panelSource()), identity.panelVariant());
  } else {
    LOG_ERR("BOARD", "Classic panel unresolved; display remains disabled");
  }
#elif PAPYRIX_TARGET_STICKY
  identity.clearPanelSelection();
  const auto probe = eink::probeStickyDisplayController(identity.profile());
  LOG_INF("BOARD", "Sticky panel: revision=%02X BUSY=%s stable=%d", probe.revision, probe.busyHigh ? "high" : "low",
          probe.stable);
  if (probe.stable) {
    identity.setPanel(
        probe.revision == 0x07 || probe.busyHigh ? eink::DisplayController::SSD2677 : eink::DisplayController::SSD1677,
        probe.revision == 0x07 ? PanelSelectionSource::Probe : PanelSelectionSource::Fallback);
  } else {
    LOG_ERR("BOARD", "Sticky panel unresolved; display remains disabled");
  }
#else
  identity.setPanel(fixedPanelFor(identity.board()), PanelSelectionSource::Fixed);
#endif
}

#endif

}  // namespace papyrix::board
