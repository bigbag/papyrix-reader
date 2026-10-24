#include <cstring>
#include <string>

#include "BoardProfiles.h"
#include "test_utils.h"
#if PAPYRIX_TARGET_MURPHY_M4
#include <HardwareIdentity.h>
#endif

namespace {

using papyrix::board::BatteryBackend;
using papyrix::board::BoardId;
using papyrix::board::BoardProfile;
using papyrix::board::InputStyle;
using papyrix::board::kPinUnused;
using papyrix::board::RtcType;
using papyrix::board::StorageTransport;
using papyrix::board::TouchController;

constexpr BoardId kAllBoards[] = {BoardId::X3, BoardId::X4, BoardId::X4Pro, BoardId::X4Classic, BoardId::Sticky,
                                  BoardId::MurphyM4};

const char* boardName(BoardId id) {
  switch (id) {
    case BoardId::X3:
      return "X3";
    case BoardId::X4:
      return "X4";
    case BoardId::X4Pro:
      return "X4 Pro";
    case BoardId::X4Classic:
      return "X4 Classic";
    case BoardId::Sticky:
      return "Sticky";
    case BoardId::MurphyM4:
      return "Murphy M4";
  }
  return "?";
}

std::string label(const char* board, const char* check) { return std::string(board) + ": " + check; }

void expectSingleMcuFamily(TestUtils::TestRunner& runner) {
  for (BoardId id : kAllBoards) {
    if (!papyrix::board::targetSupports(id)) continue;
    const BoardProfile& p = *papyrix::board::findProfile(id);
    runner.expectTrue(p.family == papyrix::board::kTargetMcuFamily,
                      label(boardName(id), "belongs to the artifact MCU family"));
  }
  runner.expectTrue(papyrix::board::bootProfile().family == papyrix::board::kTargetMcuFamily,
                    "boot profile belongs to the artifact MCU family");
}

void expectIntegralFramebufferRow(TestUtils::TestRunner& runner) {
  for (BoardId id : kAllBoards) {
    if (!papyrix::board::targetSupports(id)) continue;
    const BoardProfile& p = *papyrix::board::findProfile(id);
    runner.expectTrue(p.display.width > 0 && p.display.height > 0 && p.display.width % 8 == 0,
                      label(boardName(id), "width is positive and a multiple of 8"));
  }
}

void expectTargetFramebufferCapacity(TestUtils::TestRunner& runner) {
  uint32_t required = 0;
  for (BoardId id : kAllBoards) {
    if (!papyrix::board::targetSupports(id)) continue;
    const auto& display = papyrix::board::findProfile(id)->display;
    const uint32_t bytes = static_cast<uint32_t>(display.width / 8) * display.height;
    if (bytes > required) required = bytes;
  }
  runner.expectEq(required, papyrix::board::kTargetFrameBufferBytes,
                  "frame buffer capacity matches the largest linked panel");
}

void expectRequiredDirectGpioPins(TestUtils::TestRunner& runner) {
  for (BoardId id : kAllBoards) {
    if (!papyrix::board::targetSupports(id)) continue;
    const BoardProfile& p = *papyrix::board::findProfile(id);
    const char* name = boardName(id);
    runner.expectTrue(p.display.sclk != kPinUnused && p.display.mosi != kPinUnused && p.display.cs != kPinUnused &&
                          p.display.dc != kPinUnused && p.display.busy != kPinUnused,
                      label(name, "display uses wired SPI and control pins"));
    switch (p.storage.transport) {
      case StorageTransport::Spi:
        runner.expectTrue(p.storage.spiCs != kPinUnused && p.storage.spiMiso != kPinUnused,
                          label(name, "SPI storage wires card select and MISO"));
        break;
      case StorageTransport::Sdmmc1Bit:
        runner.expectTrue(
            p.storage.sdmmcClk != kPinUnused && p.storage.sdmmcCmd != kPinUnused && p.storage.sdmmcDat0 != kPinUnused,
            label(name, "1-bit SDMMC wires CLK, CMD, and DAT0"));
        break;
      case StorageTransport::Sdmmc4Bit:
        runner.expectTrue(p.storage.sdmmcClk != kPinUnused && p.storage.sdmmcCmd != kPinUnused &&
                              p.storage.sdmmcDat0 != kPinUnused && p.storage.sdmmcDat1 != kPinUnused &&
                              p.storage.sdmmcDat2 != kPinUnused && p.storage.sdmmcDat3 != kPinUnused,
                          label(name, "4-bit SDMMC wires all six signals"));
        break;
    }
  }
}

void expectConfiguredBackends(TestUtils::TestRunner& runner) {
  for (BoardId id : kAllBoards) {
    if (!papyrix::board::targetSupports(id)) continue;
    const BoardProfile& p = *papyrix::board::findProfile(id);
    const char* name = boardName(id);
    if (papyrix::board::hasTouch(p)) {
      runner.expectTrue(p.touch.sda != kPinUnused && p.touch.scl != kPinUnused && p.touch.i2cAddress != 0 &&
                            (p.touch.controller == TouchController::Ft6336u || p.touch.irq != kPinUnused),
                        label(name, "touch backend wires its required bus and acquisition signals"));
    } else {
      runner.expectTrue(p.touch.controller == TouchController::None, label(name, "touch is not half-advertised"));
    }
    if (papyrix::board::hasRtc(p)) {
      runner.expectTrue(p.rtc.sda != kPinUnused && p.rtc.scl != kPinUnused && p.rtc.i2cAddress != 0,
                        label(name, "RTC backend wires its I2C bus and address"));
    } else {
      runner.expectTrue(p.rtc.type == RtcType::None, label(name, "RTC is not half-advertised"));
    }
    if (papyrix::board::hasBattery(p)) {
      switch (p.battery.backend) {
        case BatteryBackend::None:
          runner.expectTrue(false, label(name, "present battery cannot use the None backend"));
          break;
        case BatteryBackend::Adc:
          runner.expectTrue(p.battery.adcPin != kPinUnused, label(name, "ADC battery backend wires its divider tap"));
          break;
        case BatteryBackend::Bq27220:
        case BatteryBackend::Cw2017:
          runner.expectTrue(p.battery.sda != kPinUnused && p.battery.scl != kPinUnused && p.battery.i2cAddress != 0,
                            label(name, "gauge battery backend wires its I2C bus and address"));
          break;
      }
    } else {
      runner.expectTrue(p.battery.backend == BatteryBackend::None && p.battery.adcPin == kPinUnused &&
                            p.battery.sda == kPinUnused && p.battery.scl == kPinUnused,
                        label(name, "absent battery uses the None backend"));
    }
    if (papyrix::board::hasFrontLight(p)) {
      runner.expectTrue(p.frontLight.pwmHz != 0 && p.frontLight.resolutionBits != 0,
                        label(name, "front light backend configures PWM"));
    } else {
      runner.expectTrue(p.frontLight.gpio == kPinUnused, label(name, "absent front light keeps its pin unassigned"));
    }
    if (papyrix::board::hasUsbDetect(p)) {
      runner.expectTrue(p.usb.detectPin != kPinUnused || p.usb.viaBatteryStatus || p.usb.nativeSerialJtag,
                        label(name, "USB detection configures a pin, gauge, or native peripheral"));
    }
    switch (p.input.style) {
      case InputStyle::AdcLadder:
        runner.expectTrue(p.input.adcPin1 != kPinUnused && p.input.adcPin2 != kPinUnused,
                          label(name, "ADC ladder input wires both ADC pins"));
        break;
      case InputStyle::DigitalButtons:
        runner.expectTrue(p.input.up != kPinUnused && p.input.down != kPinUnused,
                          label(name, "digital button input wires the page pair"));
        break;
    }
  }
}

void expectUniqueCacheKeys(TestUtils::TestRunner& runner) {
  for (BoardId a : kAllBoards) {
    for (BoardId b : kAllBoards) {
      if (a >= b) continue;
      if (!papyrix::board::targetSupports(a) || !papyrix::board::targetSupports(b)) continue;
      runner.expectTrue(std::strcmp(papyrix::board::findProfile(a)->renderCacheKey,
                                    papyrix::board::findProfile(b)->renderCacheKey) != 0,
                        label(boardName(a), (std::string("cache key differs from ") + boardName(b)).c_str()));
    }
  }
}

void expectUnlinkedProfilesAbsent(TestUtils::TestRunner& runner) {
  for (BoardId id : kAllBoards) {
    if (papyrix::board::targetSupports(id)) continue;
    runner.expectTrue(papyrix::board::findProfile(id) == nullptr,
                      label(boardName(id), "has no linked profile in this artifact"));
  }
}

void expectCapabilityMacrosMatchProfileSet(TestUtils::TestRunner& runner) {
  bool anyTouch = false, anyFrontLight = false, anyRtc = false, anyUsb = false, anySdmmc = false;
  bool anyAdcBattery = false, anyBq27220 = false, anyCw2017 = false;
  for (BoardId id : kAllBoards) {
    if (!papyrix::board::targetSupports(id)) continue;
    const BoardProfile& p = *papyrix::board::findProfile(id);
    anyTouch = anyTouch || papyrix::board::hasTouch(p);
    anyFrontLight = anyFrontLight || papyrix::board::hasFrontLight(p);
    anyRtc = anyRtc || papyrix::board::hasRtc(p);
    anyUsb = anyUsb || papyrix::board::hasUsbDetect(p);
    anySdmmc = anySdmmc || p.storage.transport != StorageTransport::Spi;
    anyAdcBattery = anyAdcBattery || (p.battery.backend == BatteryBackend::Adc && papyrix::board::hasBattery(p));
    anyBq27220 = anyBq27220 || (p.battery.backend == BatteryBackend::Bq27220 && papyrix::board::hasBattery(p));
    anyCw2017 = anyCw2017 || (p.battery.backend == BatteryBackend::Cw2017 && papyrix::board::hasBattery(p));
  }
  runner.expectTrue((PAPYRIX_CAP_TOUCH != 0) == anyTouch, "PAPYRIX_CAP_TOUCH matches the selected profile set");
  runner.expectTrue((PAPYRIX_CAP_FRONTLIGHT != 0) == anyFrontLight,
                    "PAPYRIX_CAP_FRONTLIGHT matches the selected profile set");
  runner.expectTrue((PAPYRIX_CAP_RTC != 0) == anyRtc, "PAPYRIX_CAP_RTC matches the selected profile set");
  runner.expectTrue((PAPYRIX_CAP_USB_DETECT != 0) == anyUsb, "PAPYRIX_CAP_USB_DETECT matches the selected profile set");
  runner.expectTrue((PAPYRIX_CAP_SDMMC != 0) == anySdmmc, "PAPYRIX_CAP_SDMMC matches the selected profile set");
  runner.expectTrue((PAPYRIX_CAP_BATTERY_ADC != 0) == anyAdcBattery,
                    "PAPYRIX_CAP_BATTERY_ADC matches the selected profile set");
  runner.expectTrue((PAPYRIX_CAP_BATTERY_BQ27220 != 0) == anyBq27220,
                    "PAPYRIX_CAP_BATTERY_BQ27220 matches the selected profile set");
  runner.expectTrue((PAPYRIX_CAP_BATTERY_CW2017 != 0) == anyCw2017,
                    "PAPYRIX_CAP_BATTERY_CW2017 matches the selected profile set");
}

void expectArtifactShape(TestUtils::TestRunner& runner) {
#if !PAPYRIX_TARGET_MURPHY_M4
  runner.expectFalse(papyrix::board::targetSupports(BoardId::MurphyM4), "other artifacts reject M4 pins");
#endif
#if PAPYRIX_TARGET_XTEINK_C3
  runner.expectTrue(papyrix::board::targetSupports(BoardId::X3), "C3 artifact supports X3");
  runner.expectTrue(papyrix::board::targetSupports(BoardId::X4), "C3 artifact supports X4");
  runner.expectFalse(papyrix::board::targetSupports(BoardId::X4Pro), "C3 artifact does not support X4 Pro");
  runner.expectTrue(papyrix::board::kTargetMcuFamily == papyrix::board::McuFamily::Esp32C3,
                    "C3 artifact selects the ESP32-C3 family");
  runner.expectTrue(papyrix::board::bootProfile().id == BoardId::X4, "C3 artifact boots as X4");

  const BoardProfile& x3 = *papyrix::board::findProfile(BoardId::X3);
  runner.expectTrue(std::strcmp(x3.cacheDir, "/.papyrix/cache/x3") == 0,
                    "X3 keeps the /.papyrix/cache/x3 cache directory");
  runner.expectTrue(std::strcmp(x3.renderCacheKey, "x3") == 0, "X3 render cache key is x3");
  runner.expectTrue(x3.display.width == 792 && x3.display.height == 528, "X3 panel is 792x528");
  runner.expectTrue(x3.display.spiHz == 10000000, "X3 display SPI runs at 10 MHz (UC8253 default)");
  runner.expectTrue(x3.display.sclk == 8 && x3.display.mosi == 10 && x3.display.cs == 21 && x3.display.dc == 4 &&
                        x3.display.rst == 5 && x3.display.busy == 6,
                    "X3 display uses the shared C3 display pins");
  runner.expectTrue(x3.display.power == kPinUnused && !x3.display.rotate180,
                    "X3 display has no power gate and mounts upright");
  runner.expectTrue(x3.storage.transport == StorageTransport::Spi && x3.storage.spiCs == 12 &&
                        x3.storage.spiMiso == 7 && x3.storage.powerPin == 13 && x3.storage.powerActiveHigh,
                    "X3 SD shares the display SPI bus with GPIO13 rail power");
  runner.expectTrue(x3.battery.backend == BatteryBackend::Bq27220 && x3.battery.sda == 20 && x3.battery.scl == 0 &&
                        x3.battery.i2cAddress == 0x55,
                    "X3 battery is the BQ27220 gauge on SDA20/SCL0");
  runner.expectTrue(x3.rtc.type == RtcType::Ds3231 && x3.rtc.i2cAddress == 0x68, "X3 RTC is the DS3231 at 0x68");
  runner.expectTrue(x3.usb.viaBatteryStatus && x3.usb.nativeSerialJtag && x3.usb.detectPin == kPinUnused,
                    "X3 combines gauge charging state with native USB status");
  runner.expectTrue(x3.input.style == InputStyle::AdcLadder && x3.input.adcPin1 == 1 && x3.input.adcPin2 == 2 &&
                        x3.input.power == 3 && x3.input.back == kPinUnused && x3.input.confirm == kPinUnused &&
                        x3.input.left == kPinUnused && x3.input.right == kPinUnused && x3.input.up == kPinUnused &&
                        x3.input.down == kPinUnused && !x3.input.activeHigh,
                    "X3 input is the GPIO1/GPIO2 ADC ladder with the GPIO3 power button");

  const BoardProfile& x4 = *papyrix::board::findProfile(BoardId::X4);
  runner.expectTrue(std::strcmp(x4.cacheDir, "/.papyrix/cache") == 0, "X4 keeps the /.papyrix/cache cache directory");
  runner.expectTrue(std::strcmp(x4.renderCacheKey, "") == 0, "X4 render cache key is the legacy root");
  runner.expectTrue(x4.display.width == 800 && x4.display.height == 480, "X4 panel is 800x480");
  runner.expectTrue(x4.display.spiHz == 40000000, "X4 display SPI runs at 40 MHz");
  runner.expectTrue(x4.display.sclk == 8 && x4.display.mosi == 10 && x4.display.cs == 21 && x4.display.dc == 4 &&
                        x4.display.rst == 5 && x4.display.busy == 6,
                    "X4 display uses the shared C3 display pins");
  runner.expectTrue(x4.storage.transport == StorageTransport::Spi && x4.storage.spiCs == 12 &&
                        x4.storage.spiMiso == 7 && x4.storage.powerPin == kPinUnused,
                    "X4 SD shares the display SPI bus with no rail power gate");
  runner.expectTrue(x4.battery.backend == BatteryBackend::Adc && x4.battery.adcPin == 0,
                    "X4 battery reads the GPIO0 divider");
  runner.expectTrue(x4.power.latchPin == 13 && x4.power.latchActiveHigh,
                    "X4 drives the GPIO13 battery latch active-high");
  runner.expectTrue(x4.rtc.type == RtcType::None, "X4 has no RTC");
  runner.expectTrue(x4.usb.detectPin == 20 && !x4.usb.viaBatteryStatus && x4.usb.nativeSerialJtag,
                    "X4 combines GPIO20 with native USB status");
  runner.expectTrue(x4.input.style == InputStyle::AdcLadder && x4.input.adcPin1 == 1 && x4.input.adcPin2 == 2 &&
                        x4.input.power == 3 && x4.input.back == kPinUnused && x4.input.confirm == kPinUnused &&
                        x4.input.left == kPinUnused && x4.input.right == kPinUnused && x4.input.up == kPinUnused &&
                        x4.input.down == kPinUnused && !x4.input.activeHigh,
                    "X4 input is the GPIO1/GPIO2 ADC ladder with the GPIO3 power button");
#elif PAPYRIX_TARGET_X4PRO
  runner.expectTrue(papyrix::board::targetSupports(BoardId::X4Pro), "X4 Pro artifact supports the X4 Pro");
  runner.expectTrue(!papyrix::board::targetSupports(BoardId::X3) && !papyrix::board::targetSupports(BoardId::X4),
                    "X4 Pro artifact supports no other board");
  runner.expectTrue(papyrix::board::kTargetMcuFamily == papyrix::board::McuFamily::Esp32S3,
                    "X4 Pro artifact selects the ESP32-S3 family");
  runner.expectTrue(papyrix::board::bootProfile().id == BoardId::X4Pro, "X4 Pro artifact boots as the X4 Pro");
  runner.expectTrue(papyrix::board::kBootPanelController == papyrix::eink::DisplayController::SSD1677,
                    "X4 Pro starts from the conservative panel fallback before probing");

  const BoardProfile& p = *papyrix::board::findProfile(BoardId::X4Pro);
  runner.expectTrue(std::strcmp(p.renderCacheKey, "x4pro") == 0, "X4 Pro render cache key is x4pro");
  runner.expectTrue(p.display.width == 800 && p.display.height == 480, "X4 Pro panel is 800x480");
  runner.expectTrue(p.display.spiHz == 20000000, "X4 Pro display SPI runs at 20 MHz");
  runner.expectTrue(p.display.sclk == 12 && p.display.mosi == 11 && p.display.cs == 13 && p.display.dc == 18 &&
                        p.display.rst == 14 && p.display.busy == 6,
                    "X4 Pro display uses the confirmed hardware pin sweep");
  runner.expectTrue(p.display.power == kPinUnused, "X4 Pro panel power enable stays unset (panel runs without it)");
  runner.expectTrue(p.touch.controller == TouchController::Gt911 && p.touch.sda == 39 && p.touch.scl == 38 &&
                        p.touch.irq == 10 && p.touch.rst == 4 && p.touch.i2cAddress == 0x5D,
                    "X4 Pro touch is the GT911 on the shared I2C bus");
  runner.expectTrue(p.touch.powerPin == 2 && !p.touch.powerActiveHigh,
                    "X4 Pro touch sits on the GPIO2 rail driven active-LOW");
  runner.expectTrue(p.touch.swapXY && p.touch.flipY && !p.touch.flipX && p.touch.hasHomeKey,
                    "X4 Pro digitizer mounts portrait and carries the Home key");
  runner.expectTrue(p.touch.rawMinX == 0 && p.touch.rawMaxX == 799 && p.touch.rawMinY == 0 &&
                        p.touch.rawMaxY == 479 && p.touch.i2cAddressAlt == 0x14 && p.touch.coordinatesAtByte0,
                    "X4 Pro GT911 calibration uses the post-swap panel frame");
  runner.expectTrue(p.storage.transport == StorageTransport::Sdmmc1Bit && p.storage.sdmmcClk == 41 &&
                        p.storage.sdmmcCmd == 42 && p.storage.sdmmcDat0 == 40,
                    "X4 Pro SD is native 1-bit SDMMC on CLK41/CMD42/DAT0=40");
  runner.expectTrue(p.storage.powerPin == 5 && !p.storage.powerActiveHigh,
                    "X4 Pro SD rail uses the GPIO5 enable driven active-LOW");
  runner.expectTrue(p.battery.backend == BatteryBackend::Cw2017 && p.battery.sda == 39 && p.battery.scl == 38 &&
                        p.battery.i2cAddress == 0x63 && p.battery.chargeStatusPin == 21 &&
                        p.battery.chargeStatusActiveHigh,
                    "X4 Pro battery uses CW2017 with active-high GPIO21 charge status");
  runner.expectTrue(p.rtc.type == RtcType::Bm8563 && p.rtc.i2cAddress == 0x51, "X4 Pro RTC is the BM8563 at 0x51");
  runner.expectTrue(p.touch.i2cHz == 100000 && p.battery.i2cHz == 100000 && p.rtc.i2cHz == 100000,
                    "X4 Pro limits the shared I2C bus to the stable 100 kHz mode");
  runner.expectTrue(p.frontLight.gpio == 8 && p.frontLight.warmGpio == 9 && p.frontLight.pwmHz == 10000 &&
                        p.frontLight.resolutionBits == 10 && p.frontLight.activeHigh,
                    "X4 Pro front light drives the dual LEDC PWM channels");
  runner.expectTrue(papyrix::board::hasUsbDetect(p) && p.usb.detectPin == kPinUnused &&
                        p.usb.viaBatteryStatus && p.usb.nativeSerialJtag,
                    "X4 Pro combines battery charging state with native USB status");
  runner.expectTrue(p.power.latchPin == 1 && p.power.latchActiveHigh,
                    "X4 Pro drives the GPIO1 master rail latch first");
  runner.expectTrue(p.input.style == InputStyle::DigitalButtons && p.input.up == 0 && p.input.down == 7 &&
                        p.input.power == 3 && p.input.adcPin1 == kPinUnused && p.input.adcPin2 == kPinUnused &&
                        p.input.back == kPinUnused && p.input.confirm == kPinUnused && p.input.left == kPinUnused &&
                        p.input.right == kPinUnused && !p.input.activeHigh,
                    "X4 Pro input is digital on up=GPIO0, down=GPIO7, power=GPIO3");
#elif PAPYRIX_TARGET_X4CLASSIC
  runner.expectTrue(papyrix::board::targetSupports(BoardId::X4Classic), "Classic profile is linked");
  runner.expectFalse(papyrix::board::targetSupports(BoardId::X4Pro), "Classic cannot select Pro output pins");
  runner.expectFalse(papyrix::board::targetSupports(BoardId::X3), "Classic cannot select C3 X3 pins");
  runner.expectFalse(papyrix::board::targetSupports(BoardId::X4), "Classic cannot select C3 X4 pins");
  const auto& p = papyrix::board::bootProfile();
  runner.expectTrue(p.id == BoardId::X4Classic, "Classic boots with its fixed profile");
  runner.expectFalse(papyrix::board::hasTouch(p), "Classic has no touch output ownership");
  runner.expectFalse(papyrix::board::hasFrontLight(p), "Classic button pins cannot drive frontlight PWM");
  const int8_t buttons[] = {p.input.back, p.input.confirm, p.input.left, p.input.right,
                           p.input.up, p.input.down, p.input.power};
  for (size_t i = 0; i < sizeof(buttons); ++i) {
    runner.expectTrue(buttons[i] != kPinUnused, "Every Classic action has a button");
    for (size_t j = i + 1; j < sizeof(buttons); ++j) {
      runner.expectTrue(buttons[i] != buttons[j], "Classic actions have distinct physical inputs");
    }
    for (int8_t output : {p.display.sclk, p.display.mosi, p.display.cs, p.display.dc,
                          p.display.rst, p.storage.powerPin, p.power.latchPin}) {
      runner.expectTrue(buttons[i] != output, "Classic button does not share an output pin");
    }
  }
#elif PAPYRIX_TARGET_MURPHY_M4
  const auto* m4 = papyrix::board::findProfile(BoardId::MurphyM4);
  runner.expectTrue(m4 != nullptr && papyrix::board::targetSupports(BoardId::MurphyM4),
                    "M4 selects its own profile");
  for (BoardId id : kAllBoards) {
    if (id != BoardId::MurphyM4)
      runner.expectTrue(papyrix::board::findProfile(id) == nullptr, "M4 cannot select foreign output pins");
  }
  runner.expectTrue(papyrix::board::bootProfile().id == BoardId::MurphyM4, "M4 boots its fixed profile");
  runner.expectTrue(m4 && papyrix::board::hasTouch(*m4) && papyrix::board::hasRtc(*m4) &&
                        papyrix::board::hasFrontLight(*m4),
                    "M4 exposes touch, retained time, and frontlight");
  papyrix::board::HardwareIdentity identity;
  runner.expectTrue(identity.boardSelected() && identity.panelResolved() &&
                        identity.panel() == papyrix::eink::DisplayController::SSD1677,
                    "M4 identity is usable without a foreign panel probe");
  runner.expectFalse(identity.applyBoardSelection({BoardId::Sticky, papyrix::board::BoardSelectionSource::Fixed}, {}),
                     "foreign profile cannot replace M4 identity");
  runner.expectTrue(std::strcmp(identity.cacheDir(), "/.papyrix/cache/murphy_m4") == 0,
                    "rejected selection retains M4 cache ownership");
#endif
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("BoardProfile invariants");

  expectSingleMcuFamily(runner);
  expectIntegralFramebufferRow(runner);
  expectTargetFramebufferCapacity(runner);
  expectRequiredDirectGpioPins(runner);
  expectConfiguredBackends(runner);
  expectUniqueCacheKeys(runner);
  expectUnlinkedProfilesAbsent(runner);
  expectCapabilityMacrosMatchProfileSet(runner);
  expectArtifactShape(runner);

  return runner.allPassed() ? 0 : 1;
}
