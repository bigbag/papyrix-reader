#include <Display.h>
#include <HardwareIdentity.h>
#include <SPI.h>

#include <cstdlib>
#include <vector>

#include "test_utils.h"

namespace {
struct Packet { uint8_t command; std::vector<uint8_t> data; };
std::vector<Packet> packets;
bool dataMode = false;
bool failBusy = false;
void output(int pin, int value) { if (pin == 16) dataMode = value == HIGH; }
void transfer(const uint8_t* bytes, size_t size) {
  if (!dataMode) {
    for (size_t i = 0; i < size; ++i) packets.push_back({bytes[i], {}});
  } else if (!packets.empty()) {
    packets.back().data.insert(packets.back().data.end(), bytes, bytes + size);
  }
}
const std::vector<uint8_t>& payload(uint8_t command) {
  for (auto it = packets.rbegin(); it != packets.rend(); ++it) if (it->command == command) return it->data;
  static const std::vector<uint8_t> absent;
  return absent;
}
}

int main() {
  using papyrix::hal::Display;
  TestUtils::TestRunner runner("Sticky SSD1677 sequences");
  auto& identity = papyrix::board::HardwareIdentity::instance();
  Display display;
  testSetManualMillis(0);
  testSetDigitalReadHook([](int) { return failBusy ? HIGH : LOW; });
  testSetDigitalWriteHook(output);
  testSetSpiWriteHook(transfer);
  runner.expectTrue(display.begin() == Display::InitResult::UnsupportedPanel && packets.empty(),
                    "unresolved panel sends no commands");
  identity.setPanel(papyrix::eink::DisplayController::SSD1677, papyrix::board::PanelSelectionSource::Probe);
  runner.expectTrue(display.begin() == Display::InitResult::Ok, "SSD1677 initializes");
  runner.expectTrue(payload(0x0C) == std::vector<uint8_t>({0xAE, 0xC7, 0xC3, 0xC0, 0x80}), "Sticky booster is correct");
  runner.expectEq(12, SPI.misoPin, "display startup preserves SD MISO");
  packets.clear();
  display.displayBuffer(Display::FULL_REFRESH, true);
  runner.expectTrue(payload(0x22) == std::vector<uint8_t>({0xF7}), "full uses the vendor sequence");
  packets.clear();
  display.displayBuffer(Display::FAST_REFRESH, true);
  runner.expectTrue(payload(0x22) == std::vector<uint8_t>({0xFF}), "fast uses Sticky DU waveform");
  runner.expectTrue(payload(0x3C) == std::vector<uint8_t>({0x80}), "fast parks border at VCOM");
  display.copyGrayscaleLsbBuffers(display.getFrameBuffer());
  display.copyGrayscaleMsbBuffers(display.getFrameBuffer());
  packets.clear();
  display.displayGrayBuffer(false);
  runner.expectTrue(payload(0x32).size() == 105 && payload(0x04) == std::vector<uint8_t>({0x41, 0xA8, 0x32}) &&
                        payload(0x03) == std::vector<uint8_t>({0x17}) && payload(0x2C) == std::vector<uint8_t>({0x30}),
                    "gray loads the source waveform and module voltage tail");
  runner.expectTrue(payload(0x3C) == std::vector<uint8_t>({0x80}), "gray never drives a dark border");
  bool sawPowerUp = false;
  for (const auto& packet : packets) {
    if (packet.command == 0x22 && packet.data == std::vector<uint8_t>({0xC0})) sawPowerUp = true;
  }
  runner.expectTrue(sawPowerUp, "gray settles powered rails before waveform activation");
  packets.clear();
  display.displayBuffer(Display::FAST_REFRESH, true);
  runner.expectTrue(payload(0x32).empty() && payload(0x22) == std::vector<uint8_t>({0xF7}),
                    "gray-to-mono cleans without the X4 revert waveform");
  display.copyGrayscaleLsbBuffers(display.getFrameBuffer());
  display.copyGrayscaleMsbBuffers(display.getFrameBuffer());
  display.displayGrayBuffer(false);
  failBusy = true;
  packets.clear();
  runner.expectFalse(display.deepSleep(), "failed power-down does not claim sleep");
  runner.expectTrue(payload(0x10).empty(), "failed power-down sends no deep-sleep command");
  failBusy = false;
  runner.expectTrue(display.deepSleep(), "sleep retries after BUSY recovers");
  runner.expectTrue(payload(0x10) == std::vector<uint8_t>({0x01}), "SSD1677 receives its sleep check code");
  packets.clear();
  display.recover();
  display.displayBuffer(Display::FAST_REFRESH, true);
  runner.expectTrue(payload(0x22) == std::vector<uint8_t>({0xF7}), "recovery requires a full clean baseline");
  runner.expectEq(12, SPI.misoPin, "recovery retains SD MISO");
  testSetDigitalReadHook(nullptr);
  testSetDigitalWriteHook(nullptr);
  testSetSpiWriteHook(nullptr);
  testUseRealtimeMillis();
  std::free(display.getFrameBuffer());
  return runner.allPassed() ? 0 : 1;
}
