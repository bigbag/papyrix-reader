#include <ImageConverter.h>
#include <SDCardManager.h>
#include <TargetConfig.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>

#include <algorithm>
#include <cassert>

#include "test_utils.h"


namespace {
std::string fixture(const char* name) { return TestUtils::readFile(std::string(PROGRESSIVE_FIXTURES) + "/" + name); }
uint32_t word(const std::string& bytes, size_t offset) {
  if (offset + 4 > bytes.size()) return 0;
  return uint8_t(bytes[offset]) | (uint32_t(uint8_t(bytes[offset + 1])) << 8) |
         (uint32_t(uint8_t(bytes[offset + 2])) << 16) | (uint32_t(uint8_t(bytes[offset + 3])) << 24);
}
bool blackBmp(const std::string& bytes, bool oneBit) {
  if (bytes.size() < 62 || bytes.substr(0, 2) != "BM" || word(bytes, 18) != 20 || int32_t(word(bytes, 22)) != -20 ||
      uint8_t(bytes[28]) != (oneBit ? 1 : 2))
    return false;
  const size_t offset = word(bytes, 10);
  const size_t expected = offset + 20 * (oneBit ? 4 : 8);
  return bytes.size() == expected && offset <= bytes.size() &&
         std::all_of(bytes.begin() + offset, bytes.end(), [](char byte) { return byte == 0; });
}
}  // namespace

int main() {
  TestUtils::TestRunner runner("ProgressiveJpegFactory");
  mockTaskFreeStackBytes = 8192;
  for (bool oneBit : {false, true}) {
    for (const char* name : {"black.jpg", "black-baseline.jpg", "black.png", "black.bmp"}) {
      SdMan.reset();
      const bool progressive = std::string(name) == "black.jpg";
      SdMan.registerFile("/source.wrong", fixture(name));
      ImageConvertConfig config;
      config.oneBit = oneBit;
      config.requireDithering = true;
      config.validateOutput = [&](const std::string& path) { return blackBmp(SdMan.getWrittenData(path), oneBit); };
      const bool converted = ImageConverterFactory::convertToBmp("/source.wrong", "/result.bmp", config);
      if (progressive && PAPYRIX_TARGET_XTEINK_C3) {
        runner.expectFalse(converted, "C3 rejects progressive JPEG");
        runner.expectFalse(SdMan.exists("/result.bmp"), "C3 publishes no progressive output");
      } else {
        runner.expectTrue(converted, std::string(name) + " converts by signature");
        runner.expectTrue(blackBmp(SdMan.getWrittenData("/result.bmp"), oneBit),
                          "production factory preserves black pixels");
      }
      runner.expectFalse(SdMan.exists("/result.bmp.part"), "conversion removes temporary output");
    }
  }
  const std::string source = fixture(PAPYRIX_TARGET_XTEINK_C3 ? "baseline.jpg" : "color-420.jpg");
  for (int fault = 0; fault < 7; ++fault) {
    SdMan.reset();
    testResetLargestFreeBlock();
    SdMan.registerFile("/input.jpg", source);
    SdMan.registerFile("/output.bmp", "previous-output");
    ImageConvertConfig config;
    int polls = 0;
    if (fault == 0) SdMan.setSyncResult(false);
    if (fault == 1) SdMan.setWriteLimit(70);
    if (fault == 2) SdMan.setReadLimit(12);
    if (fault == 3) config.validateOutput = [](const std::string&) { return false; };
    if (fault == 4) config.shouldAbort = [] { return true; };
    if (fault == 5) mockTaskFreeStackBytes = 2048;
    if (fault == 6) config.shouldAbort = [&] { return ++polls > 3; };
    runner.expectFalse(ImageConverterFactory::convertToBmp("/input.jpg", "/output.bmp", config),
                       "conversion fault blocks publication");
    runner.expectEq(std::string("previous-output"), SdMan.open("/output.bmp").getBuffer(),
                    "fault preserves completed output");
    runner.expectFalse(SdMan.exists("/output.bmp.part"), "fault removes partial output");
    mockTaskFreeStackBytes = 8192;
  }
  for (bool oneBit : {false, true}) {
    SdMan.reset();
    SdMan.registerFile("/baseline.jpg", fixture("baseline.jpg"));
    SdMan.setWriteLimit(oneBit ? 70 : 1);
    ImageConvertConfig config;
    config.oneBit = oneBit;
    config.maxWidth = config.maxHeight = 17;
    runner.expectFalse(ImageConverterFactory::convertToBmp("/baseline.jpg", "/short.bmp", config),
                       "baseline header or scaled row failure rejects output");
    runner.expectFalse(SdMan.exists("/short.bmp"), "baseline short write publishes no file");
    runner.expectFalse(SdMan.exists("/short.bmp.part"), "baseline short write removes partial output");
    SdMan.reset();
    SdMan.registerFile("/large.jpg", fixture("gray-maximum-baseline.jpg"));
    config.maxWidth = config.maxHeight = 1;
    runner.expectTrue(ImageConverterFactory::convertToBmp("/large.jpg", "/tiny.bmp", config),
                      "baseline maximum image reduces to one pixel");
    const std::string bmp = SdMan.getWrittenData("/tiny.bmp");
    const size_t offset = word(bmp, 10);
    runner.expectTrue(offset < bmp.size() && uint8_t(bmp[offset]) == 128, "baseline extreme reduction retains gray");
  }
#if !PAPYRIX_TARGET_XTEINK_C3
  SdMan.reset();
  SdMan.registerFile("/invalid.jpg", source.substr(0, source.size() - 2));
  runner.expectFalse(ImageConverterFactory::convertToBmp("/invalid.jpg", "/invalid.bmp"),
                     "missing EOI refuses publication");
  runner.expectFalse(SdMan.exists("/invalid.bmp"), "malformed JPEG leaves no completed file");
  runner.expectFalse(SdMan.exists("/invalid.bmp.part"), "malformed JPEG leaves no temporary file");
  SdMan.registerFile("/valid.jpg", source);
  testSetHeapPool(MALLOC_CAP_SPIRAM, 0, 0);
  runner.expectFalse(ImageConverterFactory::convertToBmp("/valid.jpg", "/valid.bmp"), "factory requires PSRAM");
  testResetLargestFreeBlock();
  runner.expectTrue(ImageConverterFactory::convertToBmp("/valid.jpg", "/valid.bmp"),
                    "factory recovers after allocation failure");
#endif
  return runner.allPassed() ? 0 : 1;
}
