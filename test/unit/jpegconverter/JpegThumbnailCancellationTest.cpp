#include "test_utils.h"

#include <ImageConverter.h>
#include <SDCardManager.h>
#include <platform_stubs.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace {

std::string makeBaselineJpeg() {
  std::string data;
  data.push_back(static_cast<char>(0xFF));
  data.push_back(static_cast<char>(0xD8));
  data.push_back(static_cast<char>(0xFF));
  data.push_back(static_cast<char>(0xC0));
  data.append(1024, 'x');
  return data;
}

uint16_t read16(const std::string& data, const size_t offset) {
  return static_cast<uint16_t>(static_cast<uint8_t>(data[offset]) |
                               static_cast<uint16_t>(static_cast<uint8_t>(data[offset + 1])) << 8);
}

int32_t read32Signed(const std::string& data, const size_t offset) {
  uint32_t value = 0;
  memcpy(&value, data.data() + offset, sizeof(value));
  return static_cast<int32_t>(value);
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("JpegThumbnailCancellation");
  SdMan.reset();
  SdMan.registerFile("/cover.img", makeBaselineJpeg());

  ImageConvertConfig config;
  config.maxWidth = 316;
  config.maxHeight = 480;
  config.oneBit = true;
  config.requireDithering = true;
  config.logTag = "TEST";
  config.validateOutput = [](const std::string& path) {
    const std::string data = SdMan.getWrittenData(path);
    return data.size() == 62 + 4 * 16 && data[0] == 'B' && data[1] == 'M';
  };

  runner.expectTrue(ImageConverterFactory::convertToBmp("/cover.img", "/cover.bmp", config),
                    "custom 1-bit JPEG conversion succeeds");
  const std::string output = SdMan.getWrittenData("/cover.bmp");
  runner.expectTrue(output.size() == 62 + 4 * 16, "JPEG output contains every declared row");
  runner.expectEq(static_cast<uint16_t>(1), read16(output, 28), "JPEG output is 1-bit");
  runner.expectEq(8, read32Signed(output, 18), "small JPEG width is not upscaled");
  runner.expectEq(-16, read32Signed(output, 22), "JPEG output is top-down at native height");

  SdMan.remove("/cover.bmp");
  int checks = 0;
  config.shouldAbort = [&checks]() { return ++checks >= 5; };
  runner.expectFalse(ImageConverterFactory::convertToBmp("/cover.img", "/cancelled.bmp", config),
                     "JPEG refill cancellation stops conversion");
  runner.expectFalse(SdMan.exists("/cancelled.bmp"), "cancelled JPEG is not published");
  runner.expectFalse(SdMan.exists("/cancelled.bmp.part"), "cancelled JPEG partial is removed");

  config.shouldAbort = []() { return true; };
  runner.expectFalse(ImageConverterFactory::convertToBmp("/cover.img", "/scan-cancelled.bmp", config),
                     "JPEG marker scan cancellation stops conversion");
  runner.expectFalse(SdMan.exists("/scan-cancelled.bmp"), "scan-cancelled JPEG is not published");

  config.shouldAbort = nullptr;
  testSetLargestFreeBlock(1);
  runner.expectFalse(ImageConverterFactory::convertToBmp("/cover.img", "/oom.bmp", config),
                     "unsafe JPEG working set is refused");
  testResetLargestFreeBlock();
  runner.expectFalse(SdMan.exists("/oom.bmp"), "OOM JPEG is not published");

  return runner.allPassed() ? 0 : 1;
}
