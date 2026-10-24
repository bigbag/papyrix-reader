#include <Bitmap.h>
#include <Display.h>
#include <GfxRenderer.h>
#include <HardwareIdentity.h>
#include <MurphyM4Batch.h>
#include <SDCardManager.h>
#include <SPI.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "test_utils.h"

namespace {
struct Packet {
  uint8_t command;
  std::vector<uint8_t> data;
};
std::vector<Packet> packets;
bool dataMode = false;
int command = -1, failedCommand = -1;
void output(int pin, int value) {
  if (pin == 6) dataMode = value == HIGH;
}
void transfer(const uint8_t* bytes, size_t size) {
  if (!dataMode) {
    for (size_t i = 0; i < size; ++i) {
      command = bytes[i];
      packets.push_back({bytes[i], {}});
    }
  } else if (!packets.empty()) {
    packets.back().data.insert(packets.back().data.end(), bytes, bytes + size);
  }
}
int busy(int pin) { return pin == 8 ? (command == failedCommand ? HIGH : LOW) : HIGH; }
const std::vector<uint8_t>& payload(uint8_t cmd) {
  for (auto it = packets.rbegin(); it != packets.rend(); ++it)
    if (it->command == cmd) return it->data;
  static const std::vector<uint8_t> absent;
  return absent;
}
std::vector<uint8_t> updates() {
  std::vector<uint8_t> result;
  for (const auto& packet : packets)
    if (packet.command == 0x22 && packet.data.size() == 1) result.push_back(packet.data[0]);
  return result;
}
void clearTrace() {
  packets.clear();
  SPI.reset();
}
std::string fourShades() {
  std::string bytes(70, '\0');
  const auto put = [&](size_t offset, uint32_t value, size_t length = 4) {
    for (size_t i = 0; i < length; ++i) bytes[offset + i] = static_cast<char>(value >> (8 * i));
  };
  bytes[0] = 'B';
  bytes[1] = 'M';
  put(2, 74);
  put(10, 70);
  put(14, 40);
  put(18, 4);
  put(22, 1);
  put(26, 1, 2);
  put(28, 2, 2);
  put(34, 4);
  put(46, 4);
  put(50, 4);
  for (uint8_t shade = 0; shade < 4; ++shade)
    for (size_t channel = 0; channel < 3; ++channel) bytes[54 + shade * 4 + channel] = static_cast<char>(shade * 85);
  bytes.append("\x1B\0\0\0", 4);
  return bytes;
}
}  // namespace

int main() {
  using namespace papyrix::board;
  using papyrix::hal::Display;
  TestUtils::TestRunner runner("M4 production SSD1677 and renderer");
#ifdef TEST_M4_BATCH_FIRST
  saveMurphyM4Batch(MurphyM4Batch::First);
  constexpr uint8_t temperature = 0x3C;
#else
  constexpr uint8_t temperature = 0x50;
#endif
  loadMurphyM4Batch();
  testSetManualMillis(0);
  testSetDigitalWriteHook(output);
  testSetDigitalReadHook(busy);
  testSetSpiWriteHook(transfer);
  for (bool failAllocation : {false, true}) {
    clearTrace();
    testSetHeapPool(MALLOC_CAP_SPIRAM, 200000, failAllocation ? 200000 : 47999);
    testSetPsramAllocationFailure(failAllocation);
    Display unavailable;
    runner.expectTrue(
        unavailable.begin() == Display::InitResult::OutOfMemory && !unavailable.supportsGrayscale() && packets.empty(),
        "fragmented or failed PSRAM leaves the display unavailable before controller traffic");
  }
  testResetLargestFreeBlock();
  Display display;
  runner.expectTrue(display.begin() == Display::InitResult::Ok, "fixed M4 panel initializes");
  runner.expectEq(uint32_t{20000000}, SPI.lastClockHz, "M4 transfers use 20 MHz");
  runner.expectTrue(payload(0x0C) == std::vector<uint8_t>({0xAE, 0xC7, 0xC3, 0xC0, 0x80}),
                    "M4 booster matches its donor");
  runner.expectTrue(payload(0x3C) == std::vector<uint8_t>({0x80}), "M4 initializes the border at VCOM");
  runner.expectTrue(display.supportsGrayscale(), "initialized M4 retains software grayscale");
  auto* frame = display.getFrameBuffer();
  std::vector<uint8_t> image(48000);
  for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>(i / 100 * 23 + i % 100 * 13);
  std::memcpy(frame, image.data(), image.size());
  clearTrace();
  display.displayBuffer(Display::FULL_REFRESH, false);
  runner.expectTrue(payload(0x24) == image && payload(0x26) == image,
                    "full planes preserve every 100-byte framebuffer row");
  runner.expectTrue(updates() == std::vector<uint8_t>{0xF7}, "full uses one absolute clean waveform");
  runner.expectTrue(payload(0x3C) == std::vector<uint8_t>{0xC0}, "normal refresh uses M4 border control");
  clearTrace();
  display.displayBuffer(Display::HALF_REFRESH, false);
  runner.expectTrue(payload(0x1A) == std::vector<uint8_t>{temperature} && updates() == std::vector<uint8_t>{0xD4},
                    "HALF uses the selected batch temperature and waveform");
  clearTrace();
  display.displayBuffer(Display::FAST_REFRESH, false);
  runner.expectTrue(updates() == std::vector<uint8_t>{0xFC}, "Fast keeps the M4 differential waveform");
  clearTrace();
  display.displayWindow(8, 7, 16, 3, false);
  std::vector<uint8_t> window;
  for (size_t row = 7; row < 10; ++row)
    window.insert(window.end(), image.begin() + row * 100 + 1, image.begin() + row * 100 + 3);
  runner.expectTrue(payload(0x24) == window && payload(0x26) == window,
                    "window sends only the selected bytes from each source row");
  runner.expectTrue(payload(0x1A) == std::vector<uint8_t>{temperature} && updates() == std::vector<uint8_t>{0xFC},
                    "window uses the batch temperature without changing Fast to HALF");
  for (const auto rect : {std::vector<uint16_t>{0, 0, 0, 8},
                          {0, 0, 8, 0},
                          {800, 0, 8, 1},
                          {0, 480, 8, 1},
                          {792, 479, 16, 1},
                          {1, 0, 8, 1},
                          {0, 0, 7, 1},
                          {65535, 0, 8, 1}}) {
    clearTrace();
    display.displayWindow(rect[0], rect[1], rect[2], rect[3], false);
    runner.expectTrue(packets.empty(), "invalid or empty window sends no controller command");
  }
  frame[47999] = 0xFE;
  image.back() = 0xFE;
  clearTrace();
  display.displayWindow(792, 479, 8, 1, false);
  runner.expectTrue(payload(0x24) == std::vector<uint8_t>{0xFE} && payload(0x26) == std::vector<uint8_t>{0xFE},
                    "bottom-right pixel stays inside the final framebuffer byte");
  clearTrace();
  display.displayWindow(0, 479, 800, 1, false);
  runner.expectTrue(
      payload(0x24) == std::vector<uint8_t>(image.end() - 100, image.end()) && payload(0x26) == payload(0x24),
      "full-width window streams exactly the final contiguous row");
  for (auto mode : {Display::FAST_REFRESH, Display::HALF_REFRESH}) {
    clearTrace();
    display.displayBuffer(mode, true);
    runner.expectTrue(
        updates() == std::vector<uint8_t>({mode == Display::FAST_REFRESH ? uint8_t{0xFC} : uint8_t{0xD4}, 0x03}),
        "turnOffScreen physically disables rails after the selected waveform");
    runner.expectTrue(payload(0x3C) == std::vector<uint8_t>{0x80}, "power-down parks the border at VCOM");
    size_t lastRam = 0, off = 0;
    for (size_t i = 0; i < packets.size(); ++i) {
      if (packets[i].command == 0x26) lastRam = i;
      if (packets[i].command == 0x22 && packets[i].data == std::vector<uint8_t>{0x03}) off = i;
    }
    runner.expectTrue(off > lastRam, "RAM synchronization finishes before analog and clock power-down");
  }
  for (uint8_t ground : {uint8_t{0}, uint8_t{0xFF}}) {
    std::memset(frame, ground, 48000);
    clearTrace();
    display.displayBuffer(Display::FULL_REFRESH, false);
    runner.expectTrue(payload(0x24) == std::vector<uint8_t>(48000, ground) && payload(0x26) == payload(0x24),
                      "dark and light frame polarity reaches both controller planes unchanged");
  }
  GfxRenderer renderer(display);
  renderer.begin();
  for (auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted, GfxRenderer::LandscapeClockwise,
                           GfxRenderer::LandscapeCounterClockwise}) {
    renderer.setOrientation(orientation);
    renderer.clearScreen();
    renderer.displayBuffer(Display::FULL_REFRESH, false);
    const int x = renderer.getScreenWidth() - 1, y = renderer.getScreenHeight() - 1;
    renderer.drawPixel(x, y);
    clearTrace();
    renderer.displayWindow(x, y, 1, 1, false);
    const uint8_t expected =
        orientation == GfxRenderer::PortraitInverted || orientation == GfxRenderer::LandscapeClockwise ? 0x7F : 0xFE;
    runner.expectTrue(payload(0x24) == std::vector<uint8_t>{expected},
                      "renderer rotates and byte-aligns the final logical pixel");
    runner.expectTrue(updates() == std::vector<uint8_t>{0xFC}, "rotated window remains a differential M4 update");
  }
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  SdMan.registerFile("/four-shades.bmp", fourShades());
  auto file = SdMan.open("/four-shades.bmp", O_RDONLY);
  Bitmap bitmap(file);
  runner.expectTrue(bitmap.parseHeaders() == BmpReaderError::Ok,
                    "image fixture parses through the production BMP reader");
  renderer.clearScreen();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.drawBitmap(bitmap, 0, 0, 4, 1);
  renderer.displayBuffer(Display::FULL_REFRESH, false);
  runner.expectEq(uint8_t{0x1F}, frame[0], "image base keeps black and gray pixels separate from white");
  clearTrace();
  renderer.clearScreen(0);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderer.drawBitmap(bitmap, 0, 0, 4, 1);
  renderer.copyGrayscaleLsbBuffers();
  renderer.clearScreen(0);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer.drawBitmap(bitmap, 0, 0, 4, 1);
  renderer.copyGrayscaleMsbBuffers();
  runner.expectTrue(payload(0x24).size() == 48000 && payload(0x24)[0] == 0x40 && payload(0x26).size() == 48000 &&
                        payload(0x26)[0] == 0x60,
                    "image AA sends distinct LSB and MSB masks without SSD2677 packing");
  renderer.displayGrayBuffer(false);
  runner.expectTrue(payload(0x32).size() == 105 && updates() == std::vector<uint8_t>{0xCC},
                    "gray uses the generic external LUT without OTP reload");
  runner.expectTrue(payload(0x32).size() == 105 && payload(0x32)[10] == 0x54 && payload(0x32)[20] == 0xAA &&
                        payload(0x32)[30] == 0xA2 && payload(0x03) == std::vector<uint8_t>{0x17} &&
                        payload(0x04) == std::vector<uint8_t>({0x41, 0xA8, 0x32}) &&
                        payload(0x2C) == std::vector<uint8_t>{0x30},
                    "gray waveform and analog values match the generic donor LUT");
  runner.expectTrue(payload(0x3C) == std::vector<uint8_t>{0xC0}, "gray uses the M4 border rather than the Sticky tail");
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  renderer.drawBitmap(bitmap, 0, 0, 4, 1);
  display.cleanupGrayscaleBuffers(frame);
  clearTrace();
  renderer.displayBuffer(Display::FAST_REFRESH, false);
  runner.expectTrue(updates() == std::vector<uint8_t>{0xF7} && payload(0x32).empty(),
                    "gray-to-mono establishes a full baseline without the X4 revert waveform");
  for (bool missingLsb : {false, true}) {
    clearTrace();
    if (missingLsb)
      display.copyGrayscaleMsbBuffers(frame);
    else
      display.copyGrayscaleLsbBuffers(frame);
    clearTrace();
    display.displayGrayBuffer(false);
    runner.expectTrue(packets.empty(), "incomplete grayscale planes cannot activate a waveform");
    display.displayBuffer(Display::FAST_REFRESH, false);
    runner.expectTrue(updates() == std::vector<uint8_t>{0xF7},
                      "incomplete staging requires a new full monochrome baseline");
  }
  display.copyGrayscaleLsbBuffers(frame);
  display.displayBuffer(Display::FULL_REFRESH, false);
  display.copyGrayscaleMsbBuffers(frame);
  clearTrace();
  display.displayGrayBuffer(false);
  runner.expectTrue(packets.empty(), "a monochrome refresh invalidates interrupted grayscale staging");
  std::vector<uint8_t> lsb(48000, 0x55), msb(48000, 0x66);
  display.displayBuffer(Display::FULL_REFRESH, false);
  clearTrace();
  display.copyGrayscaleBuffers(lsb.data(), msb.data());
  display.displayGrayBuffer(true);
  runner.expectTrue(payload(0x24) == lsb && payload(0x26) == msb && updates() == std::vector<uint8_t>{0xCF},
                    "combined masks retain their interpretation and honor grayscale power-down");
  for (auto nullPlane : {0, 1}) {
    clearTrace();
    display.copyGrayscaleBuffers(nullPlane == 0 ? nullptr : lsb.data(), nullPlane == 1 ? nullptr : msb.data());
    display.displayGrayBuffer(false);
    runner.expectTrue(packets.empty(), "missing combined plane is rejected before any RAM or LUT transfer");
  }
  for (int failedStage : {0x12, 0x46, 0x47}) {
    for (bool recovery : {false, true}) {
      clearTrace();
      failedCommand = failedStage;
      auto result = recovery ? display.recover() : display.begin();
      runner.expectTrue(result == Display::InitResult::HardwareError && !display.supportsGrayscale(),
                        "each initialization BUSY failure leaves the facade unavailable");
      runner.expectTrue(!packets.empty() && packets.back().command == failedStage && payload(0x24).empty(),
                        "initialization sends no command after its failed BUSY stage");
      clearTrace();
      display.displayWindow(0, 0, 8, 8, false);
      runner.expectTrue(payload(0x24).empty() && updates().empty(),
                        "failed runtime reinitialization cannot transfer a partial frame");
      failedCommand = -1;
      std::memcpy(frame, image.data(), image.size());
      clearTrace();
      display.displayWindow(0, 0, 8, 8, false);
      runner.expectTrue(updates() == std::vector<uint8_t>{0xF7} && payload(0x24) == image,
                        "successful recovery preserves the requested image and establishes a full baseline");
    }
  }
  for (bool combined : {false, true}) {
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen();
    renderer.drawBitmap(bitmap, 0, 0, 4, 1);
    const std::vector<uint8_t> requested(frame, frame + 48000);
    failedCommand = 0x20;
    renderer.displayBuffer(Display::FAST_REFRESH, false);
    failedCommand = -1;
    clearTrace();
    if (combined) {
      display.copyGrayscaleBuffers(lsb.data(), msb.data());
    } else {
      renderer.clearScreen(0);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      renderer.drawBitmap(bitmap, 0, 0, 4, 1);
      renderer.copyGrayscaleLsbBuffers();
      renderer.clearScreen(0);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      renderer.drawBitmap(bitmap, 0, 0, 4, 1);
      renderer.copyGrayscaleMsbBuffers();
    }
    renderer.displayGrayBuffer(false);
    runner.expectTrue(packets.empty(), "a failed monochrome image cannot stage or activate grayscale masks");
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen();
    renderer.drawBitmap(bitmap, 0, 0, 4, 1);
    clearTrace();
    renderer.displayBuffer(Display::FAST_REFRESH, false);
    runner.expectTrue(!packets.empty() && packets.front().command == 0x12 && updates() == std::vector<uint8_t>{0xF7} &&
                          payload(0x24) == requested && payload(0x26) == requested,
                      "the next monochrome render resets and restores the requested full image, not masks");
    clearTrace();
    display.copyGrayscaleBuffers(lsb.data(), msb.data());
    renderer.displayGrayBuffer(false);
    runner.expectTrue(updates() == std::vector<uint8_t>{0xCC} && payload(0x24) == lsb && payload(0x26) == msb,
                      "a successful monochrome recovery permits grayscale again");
  }
  failedCommand = 0x20;
  clearTrace();
  display.displayBuffer(Display::FAST_REFRESH, false);
  runner.expectFalse(display.supportsGrayscale(), "runtime BUSY failure invalidates initialized controller state");
  failedCommand = -1;
  std::memcpy(frame, image.data(), image.size());
  clearTrace();
  display.displayBuffer(Display::FAST_REFRESH, false);
  runner.expectTrue(payload(0x12).empty() && !packets.empty() && packets.front().command == 0x12 &&
                        updates() == std::vector<uint8_t>{0xF7} && payload(0x24) == image,
                    "the next request resets the controller and preserves the current framebuffer");
  clearTrace();
  display.displayBuffer(Display::FAST_REFRESH, false);
  runner.expectTrue(updates() == std::vector<uint8_t>{0xFC}, "successful full recovery restores differential updates");
  clearTrace();
  runner.expectTrue(
      display.deepSleep() && payload(0x10) == std::vector<uint8_t>{0x03} && updates() == std::vector<uint8_t>{0x03},
      "M4 deep sleep powers down before discarding controller RAM");
  testSetSpiWriteHook(nullptr);
  testSetDigitalWriteHook(nullptr);
  testSetDigitalReadHook(nullptr);
  testUseRealtimeMillis();
  std::free(display.getFrameBuffer());
  return runner.allPassed() ? 0 : 1;
}
