#include "test_utils.h"

#include <ImageConverter.h>
#include <SDCardManager.h>

#include <cstdint>
#include <string>

namespace {

const std::string kPngSignature("\x89PNG\r\n\x1A\n", 8);

void registerSources() {
  SdMan.reset();
  SdMan.registerFile("/neutral.img", std::string("\xFF\xD8jpeg", 6));
  SdMan.registerFile("/wrong.png", std::string("\xFF\xD8jpeg", 6));
  SdMan.registerFile("/source.png.bin", kPngSignature + "payload");
  SdMan.registerFile("/source.bmp.bin", "BMpayload");
  SdMan.registerFile("/unknown.img", "GIF89a");
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("ImageConverterSignature");
  registerSources();

  runner.expectTrue(ImageConverterFactory::detectFormat("/neutral.img") == ImageFormat::Jpeg,
                    "JPEG signature ignores neutral extension");
  runner.expectTrue(ImageConverterFactory::detectFormat("/wrong.png") == ImageFormat::Jpeg,
                    "signature overrides misleading extension");
  runner.expectTrue(ImageConverterFactory::detectFormat("/source.png.bin") == ImageFormat::Png,
                    "PNG signature detected");
  runner.expectTrue(ImageConverterFactory::detectFormat("/source.bmp.bin") == ImageFormat::Bmp,
                    "BMP signature detected");
  runner.expectTrue(ImageConverterFactory::detectFormat("/unknown.img") == ImageFormat::Unknown,
                    "unknown signature rejected");
  runner.expectTrue(ImageConverterFactory::detectFormat("/missing.img") == ImageFormat::Unknown,
                    "missing source rejected");

  ImageConvertConfig config;
  config.logTag = "TEST";
  runner.expectTrue(ImageConverterFactory::convertToBmp("/wrong.png", "/jpeg.bmp", config),
                    "misleading extension converts by JPEG signature");
  runner.expectTrue(SdMan.getWrittenData("/jpeg.bmp") == "JPEG", "JPEG converter selected");

  runner.expectTrue(ImageConverterFactory::convertToBmp("/source.png.bin", "/png.bmp", config),
                    "neutral extension converts by PNG signature");
  runner.expectTrue(SdMan.getWrittenData("/png.bmp") == "PNG", "PNG converter selected");

  // Invalid BMP payload must be rejected by the real converter (not raw-copied)
  runner.expectFalse(ImageConverterFactory::convertToBmp("/source.bmp.bin", "/copy.bmp", config),
                     "invalid BMP payload is rejected");
  runner.expectFalse(SdMan.exists("/copy.bmp"), "rejected BMP publishes nothing");

  config.validateOutput = [](const std::string&) { return false; };
  runner.expectFalse(ImageConverterFactory::convertToBmp("/neutral.img", "/rejected.bmp", config),
                     "validator rejection fails conversion");
  runner.expectFalse(SdMan.exists("/rejected.bmp"), "validator rejection does not publish final output");
  runner.expectFalse(SdMan.exists("/rejected.bmp.part"), "validator rejection removes partial output");

  config.validateOutput = [](const std::string& path) {
    return path == "/accepted.bmp.part" && SdMan.getWrittenData(path) == "JPEG";
  };
  runner.expectTrue(ImageConverterFactory::convertToBmp("/neutral.img", "/accepted.bmp", config),
                    "validator accepts complete output");
  runner.expectTrue(SdMan.getWrittenData("/accepted.bmp") == "JPEG", "validated output committed");
  runner.expectFalse(SdMan.exists("/accepted.bmp.part"), "committed partial path removed");

  runner.expectFalse(ImageConverterFactory::convertToBmp("/unknown.img", "/unknown.bmp", {}),
                     "unknown source is not converted");
  runner.expectFalse(SdMan.exists("/unknown.bmp"), "unknown source does not create output");

  return runner.allPassed() ? 0 : 1;
}
