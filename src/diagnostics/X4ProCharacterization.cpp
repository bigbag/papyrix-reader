#include "X4ProCharacterization.h"

#include <TargetConfig.h>

#if PAPYRIX_X4PRO_CHARACTERIZE

#include <Arduino.h>
#include <BoardProfiles.h>
#include <BoardSelector.h>
#include <FrontLightBackend.h>
#include <HardwareIdentity.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <TouchBackend.h>
#include <Uc8179X4ProDriver.h>
#include <Uc8279SpiBus.h>
#include <Uc8279X4ProDriver.h>
#include <X3DisplayProbe.h>
#include <X4ProBoard.h>
#include <esp_heap_caps.h>

#include <cstring>

#include "core/Core.h"
#include "hal/Power.h"

namespace papyrix::diagnostics {
namespace {

const board::BoardProfile* profile;
board::TouchBackend touch;
board::FrontLightBackend frontLight;
bool touchReady;
bool frontLightReady;
uint8_t previousButtons = 0xFF;
char commandLine[16];
size_t commandLength;
bool commandOverflow;

bool pressed(int8_t pin) {
  if (pin == board::kPinUnused) return false;
  return digitalRead(pin) == (profile->input.activeHigh ? HIGH : LOW);
}

uint8_t readButtons() {
  return static_cast<uint8_t>((pressed(profile->input.up) ? 1 : 0) | (pressed(profile->input.down) ? 2 : 0) |
                              (pressed(profile->input.power) ? 4 : 0));
}

void printStatus() {
  const auto battery = core.battery.readStatus();
  const auto usb = core.usb.readStatus();
  board::DateTime rtc;
  const bool rtcReadable = core.clock.readRtc(rtc);

  Serial.printf("STATUS sd=%u touch=%u frontlight=%u usb_available=%u usb_connected=%u\n", SdMan.ready(), touchReady,
                frontLightReady, usb.available, usb.connected);
  Serial.printf("BATTERY supported=%u percent_known=%u percent=%u mv_known=%u mv=%u charging_known=%u charging=%u\n",
                battery.supported, battery.percentageKnown, static_cast<unsigned>(battery.percentage),
                battery.millivoltsKnown, static_cast<unsigned>(battery.millivolts), battery.chargingKnown,
                battery.charging);
  Serial.printf("RTC available=%u readable=%u value=%04u-%02u-%02uT%02u:%02u:%02u\n", core.clock.isRtcAvailable(),
                rtcReadable, static_cast<unsigned>(rtc.year), static_cast<unsigned>(rtc.month),
                static_cast<unsigned>(rtc.day), static_cast<unsigned>(rtc.hour), static_cast<unsigned>(rtc.minute),
                static_cast<unsigned>(rtc.second));
}

void setFrontLight(uint8_t brightness, uint8_t warmth, const char* name) {
  if (!frontLightReady) {
    Serial.println("FRONTLIGHT unavailable");
    return;
  }
  frontLight.write(brightness, warmth);
  Serial.printf("FRONTLIGHT %s\n", name);
}

void printProbeSample(const char* name, const eink::X3DisplayProbeSample& sample) {
  Serial.printf("PANEL %s flg=%02X ver=%02X %02X %02X %02X %02X\n", name, sample.flg, sample.ver[0], sample.ver[1],
                sample.ver[2], sample.ver[3], sample.ver[4]);
}

void probePanel() {
  const auto& display = profile->display;
  const eink::X3DisplayProbePins pins{display.sclk, display.mosi, display.cs, display.dc, display.rst, display.busy};
  pinMode(display.busy, INPUT);
  const int busyBefore = digitalRead(display.busy);

  Serial.println("PANEL PROBE begin read_only=1 spi=mode0-bitbang-500kHz commands=71,70,A2 reset_low_ms=1,50");
  const eink::X3DisplayProbeReport report = eink::probeX4ProDisplayController(pins);
  const int busyAfter = digitalRead(display.busy);

  printProbeSample("pass1", report.pass1);
  printProbeSample("pass2", report.pass2);
  Serial.printf("PANEL mtp_valid=%u mtp_repeatable=%u", report.mtpValid, report.mtpRepeatable);
  if (report.mtpValid) {
    Serial.print(" mtp=");
    for (uint8_t byte : report.mtp) Serial.printf("%02X", byte);
  }
  Serial.println();

  const char* result = "inconclusive";
  if (report.verdict == eink::X3DisplayVerdict::UC8279Confirmed) {
    result = "uc81xx-response";
  } else if (report.verdict == eink::X3DisplayVerdict::UC8253StableDefault) {
    result = "no-uc81xx-response";
  }
  Serial.printf("PANEL RESULT %s busy_before=%d busy_after=%d pins=released refresh=disabled\n", result, busyBefore,
                busyAfter);
}
void releasePanelPins() {
  const auto& display = profile->display;
  SPI.end();
  pinMode(display.sclk, INPUT);
  pinMode(display.mosi, INPUT);
  pinMode(display.cs, INPUT);
  pinMode(display.dc, INPUT);
  pinMode(display.rst, INPUT);
  pinMode(display.busy, INPUT);
}

void refreshWhitePanel() {
  const auto& display = profile->display;
  const eink::X3DisplayProbePins pins{display.sclk, display.mosi, display.cs, display.dc, display.rst, display.busy};
  const eink::X3DisplayProbeReport report = eink::probeX4ProDisplayController(pins);
  const eink::X4ProPanelVariant variant = eink::classifyX4ProPanel(report);
  printProbeSample("white-check", report.pass2);
  if (variant == eink::X4ProPanelVariant::Ssd1677) {
    Serial.println("PANEL WHITE result=wrong_controller pins=released refresh=not_started");
    return;
  }
  const char* controller = variant == eink::X4ProPanelVariant::Uc8179 ? "UC8179" : "UC8279";

  constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  Serial.printf("PANEL WHITE begin controller=%s lut=%02X size=800x480 addressed=800x600 spi=%lu waveform=otp-full\n",
                controller, report.pass2.ver[2], static_cast<unsigned long>(display.spiHz));
  if (frontLightReady) frontLight.write(0, 50);

  if (heap_caps_get_largest_free_block(caps) < eink::Uc8279X4ProDriver::BUFFER_SIZE) {
    Serial.println("PANEL WHITE result=out_of_memory pins=released refresh=not_started");
    return;
  }
  uint8_t* frame = static_cast<uint8_t*>(heap_caps_malloc(eink::Uc8279X4ProDriver::BUFFER_SIZE, caps));
  if (frame == nullptr) {
    Serial.println("PANEL WHITE result=out_of_memory pins=released refresh=not_started");
    return;
  }
  memset(frame, 0xFF, eink::Uc8279X4ProDriver::BUFFER_SIZE);

  SPI.begin(display.sclk, -1, display.mosi, display.cs);
  const SPISettings settings(display.spiHz, MSBFIRST, SPI_MODE0);
  pinMode(display.cs, OUTPUT);
  pinMode(display.dc, OUTPUT);
  pinMode(display.busy, INPUT);
  digitalWrite(display.cs, HIGH);
  digitalWrite(display.dc, HIGH);

  eink::Uc8279SpiBus bus(display.cs, display.dc, display.rst, display.busy, settings);
  const unsigned long start = millis();
  bool initialized = false;
  bool refreshed = false;
  bool sleeping = false;
  if (variant == eink::X4ProPanelVariant::Uc8179) {
    auto& driver = eink::uc8179X4ProDriver();
    initialized = driver.begin(bus);
    if (initialized) {
      refreshed = driver.display(bus, frame, eink::Uc8179RefreshMode::Full, true);
      sleeping = driver.deepSleep(bus);
    }
  } else {
    auto& driver = eink::uc8279X4ProDriver();
    initialized = driver.begin(bus, report.pass2.ver[2]);
    if (initialized) {
      refreshed = driver.display(bus, frame, eink::Uc8279X4RefreshMode::Full, true);
      sleeping = driver.deepSleep(bus);
    }
  }

  releasePanelPins();
  heap_caps_free(frame);
  Serial.printf("PANEL WHITE result=%s initialized=%u refreshed=%u deep_sleep=%u pins=released elapsed_ms=%lu\n",
                initialized && refreshed && sleeping ? "ok" : "failed", initialized, refreshed, sleeping,
                millis() - start);
}

void handleCommand(X4ProCommand command) {
  switch (command) {
    case X4ProCommand::Status:
      printStatus();
      break;
    case X4ProCommand::LightOff:
      setFrontLight(0, 50, "off");
      break;
    case X4ProCommand::CoolLight:
      setFrontLight(20, 0, "cool 20%");
      break;
    case X4ProCommand::WarmLight:
      setFrontLight(20, 100, "warm 20%");
      break;
    case X4ProCommand::Sleep:
      Serial.println("SLEEP entering");
      Serial.flush();
      frontLight.end();
      touch.shutdown();
      hal::enterDeepSleepWithHardwareShutdown(core.usb.isConnected());
      break;
    case X4ProCommand::PanelProbe:
      probePanel();
      break;
    case X4ProCommand::WhiteRefresh:
      refreshWhitePanel();
      break;
    case X4ProCommand::None:
      break;
  }
}

void handleSerialByte(char value) {
  if (value == '\r' || value == '\n') {
    if (commandLength != 0 && !commandOverflow) {
      handleCommand(parseX4ProCommand(std::string_view(commandLine, commandLength)));
    }
    commandLength = 0;
    commandOverflow = false;
    return;
  }

  if (commandLength < sizeof(commandLine)) {
    commandLine[commandLength++] = value;
  } else {
    commandOverflow = true;
  }
}

}  // namespace

void beginX4ProCharacterization() {
  auto& identity = board::HardwareIdentity::instance();
  board::selectBoard(identity);
  profile = &identity.profile();
  board::x4pro::prepareCharacterization(*profile);
  board::selectPanel(identity);

  Serial.println("X4PRO CHARACTERIZATION: DISPLAY REFRESH COMMAND-GATED");
  Serial.printf("BOARD %s display=%ux%u buttons=%d,%d,%d touch_i2c=%d,%d sdmmc=%d,%d,%d\n", profile->name,
                static_cast<unsigned>(profile->display.width), static_cast<unsigned>(profile->display.height),
                profile->input.up, profile->input.down, profile->input.power, profile->touch.sda, profile->touch.scl,
                profile->storage.sdmmcClk, profile->storage.sdmmcCmd, profile->storage.sdmmcDat0);

  const bool sdReady = SdMan.begin();
  core.battery.init();
  core.usb.init(core.battery);
  core.clock.init();
  frontLightReady = frontLight.begin();
  touchReady = touch.begin(*profile);

  Serial.printf("INIT sd=%u touch=%u frontlight=%u\n", sdReady, touchReady, frontLightReady);
  printStatus();
  Serial.println(
      "COMMANDS ? status, 0 light off, 1 cool 20%, 2 warm 20%, p read-only panel probe, white one full white "
      "refresh, s sleep");
}

void updateX4ProCharacterization() {
  while (Serial.available()) handleSerialByte(static_cast<char>(Serial.read()));

  const uint8_t buttons = readButtons();
  if (buttons != previousButtons) {
    previousButtons = buttons;
    Serial.printf("BUTTONS up=%u down=%u power=%u\n", (buttons & 1) != 0, (buttons & 2) != 0, (buttons & 4) != 0);
  }

  const auto sample = touch.poll();
  if (sample.fresh) {
    Serial.printf("TOUCH ok=%u contacts=%u raw=%u,%u\n", sample.controllerOk,
                  static_cast<unsigned>(sample.contactCount), static_cast<unsigned>(sample.rawX),
                  static_cast<unsigned>(sample.rawY));
  }
  delay(10);
}

}  // namespace papyrix::diagnostics

#endif
