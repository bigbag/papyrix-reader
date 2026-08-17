#include "test_utils.h"

#include <ImageConverter.h>
#include <SDCardManager.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace {

const std::string kPngSource = std::string("\x89PNG\r\n\x1A\n", 8) + "fake";

uint16_t read16(const std::string& data, const size_t offset) {
  return static_cast<uint16_t>(static_cast<uint8_t>(data[offset]) |
                               static_cast<uint16_t>(static_cast<uint8_t>(data[offset + 1])) << 8);
}

int32_t read32Signed(const std::string& data, const size_t offset) {
  uint32_t value = 0;
  memcpy(&value, data.data() + offset, sizeof(value));
  return static_cast<int32_t>(value);
}

ImageConvertConfig thumbnailConfig() {
  ImageConvertConfig config;
  config.maxWidth = 316;
  config.maxHeight = 480;
  config.oneBit = true;
  config.requireDithering = true;
  config.logTag = "TEST";
  config.validateOutput = [](const std::string& path) {
    const std::string data = SdMan.getWrittenData(path);
    return data.size() == 70 && data[0] == 'B' && data[1] == 'M';
  };
  return config;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("PngOneBit");
  SdMan.reset();
  SdMan.registerFile("/cover.img", kPngSource);

  ImageConvertConfig config = thumbnailConfig();
  runner.expectTrue(ImageConverterFactory::convertToBmp("/cover.img", "/cover.bmp", config),
                    "PNG one-bit conversion succeeds");
  const std::string first = SdMan.getWrittenData("/cover.bmp");
  runner.expectEq(static_cast<uint16_t>(1), read16(first, 28), "PNG output is 1-bit");
  runner.expectEq(2, read32Signed(first, 18), "small PNG width is not upscaled");
  runner.expectEq(-2, read32Signed(first, 22), "PNG output is top-down at native height");
  runner.expectTrue(first.size() == 70, "PNG output contains every declared row");
  runner.expectTrue((static_cast<uint8_t>(first[66]) & 0x80U) != 0,
                    "transparent black blends to white");

  SdMan.remove("/cover.bmp");
  runner.expectTrue(ImageConverterFactory::convertToBmp("/cover.img", "/second.bmp", config),
                    "repeat PNG conversion succeeds");
  runner.expectTrue(first == SdMan.getWrittenData("/second.bmp"), "PNG Atkinson output is deterministic");

  int checks = 0;
  config.shouldAbort = [&checks]() { return ++checks >= 3; };
  runner.expectFalse(ImageConverterFactory::convertToBmp("/cover.img", "/cancelled.bmp", config),
                     "final-feed cancellation rejects PNG output");
  runner.expectFalse(SdMan.exists("/cancelled.bmp"), "cancelled PNG is not published");
  runner.expectFalse(SdMan.exists("/cancelled.bmp.part"), "cancelled PNG partial is removed");

  return runner.allPassed() ? 0 : 1;
}
