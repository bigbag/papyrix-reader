#include "test_utils.h"

#include <BitmapTurnCache.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <blocks/ImageBlock.h>
#include <platform_stubs.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {
template <typename T>
void append(std::string& data, T value) {
  data.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

std::string makeBmp(bool inverse) {
  constexpr int32_t width = 24;
  constexpr int32_t height = 20;
  constexpr uint32_t pixels = width * height * 3;
  std::string data = "BM";
  append(data, uint32_t{54 + pixels});
  append(data, uint32_t{0});
  append(data, uint32_t{54});
  append(data, uint32_t{40});
  append(data, width);
  append(data, -height);
  append(data, uint16_t{1});
  append(data, uint16_t{24});
  append(data, uint32_t{0});
  append(data, pixels);
  append(data, int32_t{2835});
  append(data, int32_t{2835});
  append(data, uint32_t{0});
  append(data, uint32_t{0});
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint8_t shade = static_cast<uint8_t>((x * 37 + y * 19) % 256);
      for (int channel = 0; channel < 3; ++channel) data.push_back(inverse ? ~shade : shade);
    }
  }
  return data;
}

std::vector<uint8_t> drawPair(GfxRenderer& renderer, papyrix::hal::Display& display, ImageBlock& first,
                              ImageBlock& second, GfxRenderer::RenderMode mode) {
  renderer.setRenderMode(mode);
  renderer.clearScreen(0xFF);
  first.render(renderer, 0, 4, 7);
  second.render(renderer, 0, 40, 7);
  return {renderer.getFrameBuffer(), renderer.getFrameBuffer() + display.getBufferSize()};
}
}  // namespace

int main() {
  TestUtils::TestRunner runner("BitmapTurnCache");
  SdMan.reset();
  SdMan.registerFile("/one.bmp", makeBmp(false));
  SdMan.registerFile("/two.bmp", makeBmp(true));
  ImageBlock first("/one.bmp", 24, 20);
  ImageBlock second("/two.bmp", 24, 20);
  papyrix::hal::Display referenceDisplay(8, 10, 21, 4, 5, 6);
  papyrix::hal::Display cachedDisplay(8, 10, 21, 4, 5, 6);
  GfxRenderer reference(referenceDisplay);
  GfxRenderer cached(cachedDisplay);
  reference.begin();
  cached.begin();
  testSetHeapPool(MALLOC_CAP_INTERNAL, 4, 4);
  testSetHeapPool(MALLOC_CAP_SPIRAM, 0, 0);

  const std::array<GfxRenderer::RenderMode, 5> passes = {
      GfxRenderer::BW, GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB, GfxRenderer::BW};
  std::array<std::vector<uint8_t>, 5> expected;
  for (size_t i = 0; i < passes.size(); ++i) expected[i] = drawPair(reference, referenceDisplay, first, second, passes[i]);
  const size_t streamedReads = gMockBufferReadCalls;

  testSetHeapPool(MALLOC_CAP_SPIRAM, 32 * 1024, 32 * 1024);
  {
    BitmapTurnCache turn(cached);
    size_t readsAfterFirst = 0;
    const size_t opensBefore = SdMan.readOpenCount();
    for (size_t i = 0; i < passes.size(); ++i) {
      const auto actual = drawPair(cached, cachedDisplay, first, second, passes[i]);
      runner.expectTrue(actual == expected[i], "every dithered pass matches streamed pixels");
      if (i == 0) {
        readsAfterFirst = gMockBufferReadCalls;
        runner.expectEq<size_t>(2, SdMan.readOpenCount() - opensBefore, "each image opened once");
      } else {
        runner.expectEq<size_t>(readsAfterFirst, gMockBufferReadCalls, "later passes do not read SD");
      }
    }
    runner.expectTrue(streamedReads > gMockBufferReadCalls - streamedReads, "reused pass saves SD reads");
  }
  runner.expectTrue(cached.bitmapTurnCache() == nullptr, "turn releases cache before next page");

  testSetPsramAllocationFailure(true);
  FsFile directFile = SdMan.open("/one.bmp", O_RDONLY);
  {
    Bitmap direct(directFile);
    runner.expectTrue(direct.parseHeaders() == BmpReaderError::Ok, "direct bitmap parses");
    runner.expectFalse(direct.preloadAllRows(), "S3 direct preload does not use internal RAM on PSRAM failure");
    testSetPsramAllocationFailure(false);
    runner.expectTrue(direct.preloadAllRows() && direct.preloadedRow(0),
                      "S3 direct preload uses available PSRAM");
  }
  directFile.close();
  testSetPsramAllocationFailure(true);
  {
    BitmapTurnCache turn(cached);
    const size_t readsBefore = gMockBufferReadCalls;
    for (size_t i = 0; i < 2; ++i) {
      runner.expectTrue(drawPair(cached, cachedDisplay, first, second, passes[i]) == expected[i],
                        "PSRAM failure streams unchanged pixels");
    }
    runner.expectTrue(gMockBufferReadCalls > readsBefore, "PSRAM failure reads source rows");
  }
  testResetLargestFreeBlock();
  return runner.allPassed() ? 0 : 1;
}
