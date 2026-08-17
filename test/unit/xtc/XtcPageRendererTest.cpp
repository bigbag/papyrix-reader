#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Xtc/XtcParser.h>
#include <Xtc/XtcTypes.h>
#include <XtcPageRenderer.h>
#include <platform_stubs.h>

#include <cstring>
#include <string>
#include <vector>

#include "test_utils.h"

namespace {

std::string buildXtcFile(uint32_t magic, const std::vector<uint8_t>& pixels, uint16_t width = 8,
                         uint16_t height = 8) {
  constexpr size_t headerSize = sizeof(xtc::XtcHeader);
  constexpr size_t titleSize = 128;
  constexpr size_t authorSize = 64;
  constexpr size_t pageTableOffset = headerSize + titleSize + authorSize;
  constexpr size_t pageDataOffset = pageTableOffset + sizeof(xtc::PageTableEntry);
  const size_t pageDataSize = sizeof(xtc::XtgPageHeader) + pixels.size();

  std::string file(pageDataOffset + pageDataSize, '\0');
  auto* data = reinterpret_cast<uint8_t*>(&file[0]);

  auto* header = reinterpret_cast<xtc::XtcHeader*>(data);
  header->magic = magic;
  header->versionMajor = 1;
  header->pageCount = 1;
  header->hasMetadata = 1;
  header->pageTableOffset = pageTableOffset;
  header->dataOffset = pageDataOffset;

  auto* page = reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset);
  page->dataOffset = pageDataOffset;
  page->dataSize = pageDataSize;
  page->width = width;
  page->height = height;

  auto* pageHeader = reinterpret_cast<xtc::XtgPageHeader*>(data + pageDataOffset);
  pageHeader->magic = magic == xtc::XTCH_MAGIC ? xtc::XTH_MAGIC : xtc::XTG_MAGIC;
  pageHeader->width = width;
  pageHeader->height = height;
  pageHeader->dataSize = pixels.size();

  memcpy(data + pageDataOffset + sizeof(xtc::XtgPageHeader), pixels.data(), pixels.size());
  return file;
}

bool isPhysicalBlack(const uint8_t* buffer, const uint16_t stride, const uint16_t x, const uint16_t y) {
  const size_t byteOffset = static_cast<size_t>(y) * stride + x / 8;
  return (buffer[byteOffset] & static_cast<uint8_t>(1U << (7 - x % 8))) == 0;
}

std::string buildMixedDimensionXtc() {
  constexpr uint16_t widths[] = {8, 16};
  constexpr uint16_t heights[] = {8, 9};
  constexpr size_t pageCount = 2;
  constexpr size_t headerSize = sizeof(xtc::XtcHeader);
  constexpr size_t pageTableOffset = headerSize + 128 + 64;
  constexpr size_t pageDataOffset = pageTableOffset + pageCount * sizeof(xtc::PageTableEntry);
  const size_t firstPageSize = sizeof(xtc::XtgPageHeader) + xtc::xtgBitmapSize(widths[0], heights[0]);
  const size_t secondPageSize = sizeof(xtc::XtgPageHeader) + xtc::xtgBitmapSize(widths[1], heights[1]);

  std::string file(pageDataOffset + firstPageSize + secondPageSize, '\0');
  auto* data = reinterpret_cast<uint8_t*>(&file[0]);
  auto* header = reinterpret_cast<xtc::XtcHeader*>(data);
  header->magic = xtc::XTC_MAGIC;
  header->versionMajor = 1;
  header->pageCount = pageCount;
  header->pageTableOffset = pageTableOffset;
  header->dataOffset = pageDataOffset;

  size_t offset = pageDataOffset;
  for (size_t i = 0; i < pageCount; i++) {
    const size_t bitmapSize = xtc::xtgBitmapSize(widths[i], heights[i]);
    const size_t pageSize = sizeof(xtc::XtgPageHeader) + bitmapSize;
    auto* entry =
        reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset + i * sizeof(xtc::PageTableEntry));
    entry->dataOffset = offset;
    entry->dataSize = pageSize;
    entry->width = widths[i];
    entry->height = heights[i];

    auto* pageHeader = reinterpret_cast<xtc::XtgPageHeader*>(data + offset);
    pageHeader->magic = xtc::XTG_MAGIC;
    pageHeader->width = widths[i];
    pageHeader->height = heights[i];
    pageHeader->dataSize = bitmapSize;
    memset(data + offset + sizeof(*pageHeader), 0xFF, bitmapSize);
    offset += pageSize;
  }

  return file;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("XtcPageRenderer Tests");
  EInkDisplay display(0, 0, 0, 0, 0, 0);
  GfxRenderer gfx(display);
  gfx.begin();
  papyrix::XtcPageRenderer renderer(gfx);

  {
    SdMan.reset();
    SdMan.registerFile("/test.xtc", buildXtcFile(xtc::XTC_MAGIC, std::vector<uint8_t>(8, 0xFF)));

    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/test.xtc") == xtc::XtcError::OK, "1-bit: parser opens");

    std::vector<papyrix::XtcPageRenderer::RefreshRequest> requests;
    auto result = renderer.render(
        parser, 0, [&requests](papyrix::XtcPageRenderer::RefreshRequest request) { requests.push_back(request); });

    runner.expectTrue(result == papyrix::XtcPageRenderer::RenderResult::Success, "1-bit: renders");
    runner.expectEq(size_t{1}, requests.size(), "1-bit: one refresh request");
    runner.expectTrue(requests[0] == papyrix::XtcPageRenderer::RefreshRequest::Cadenced,
                      "1-bit: follows reader cadence");
  }

  {
    SdMan.reset();
    constexpr uint16_t width = 480;
    constexpr uint16_t height = 800;
    SdMan.registerFile("/full.xtc",
                       buildXtcFile(xtc::XTC_MAGIC, std::vector<uint8_t>(xtc::xtgBitmapSize(width, height), 0xFF),
                                    width, height));

    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/full.xtc") == xtc::XtcError::OK, "full-size 1-bit: parser opens");
    std::vector<papyrix::XtcPageRenderer::RefreshRequest> requests;
    testSetLargestFreeBlock(5120);
    const auto result = renderer.render(
        parser, 0, [&requests](papyrix::XtcPageRenderer::RefreshRequest request) { requests.push_back(request); });
    testResetLargestFreeBlock();
    runner.expectTrue(result == papyrix::XtcPageRenderer::RenderResult::Success,
                      "full-size 1-bit: renders with 4KB scratch");
    runner.expectEq(size_t{1}, requests.size(), "full-size 1-bit: one refresh");
  }

  {
    SdMan.reset();
    SdMan.registerFile("/test.xtch", buildXtcFile(xtc::XTCH_MAGIC, std::vector<uint8_t>(16, 0x00)));

    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/test.xtch") == xtc::XtcError::OK, "2-bit: parser opens");

    std::vector<papyrix::XtcPageRenderer::RefreshRequest> requests;
    auto result = renderer.render(
        parser, 0, [&requests](papyrix::XtcPageRenderer::RefreshRequest request) { requests.push_back(request); });

    runner.expectTrue(result == papyrix::XtcPageRenderer::RenderResult::Success, "2-bit: renders");
    runner.expectEq(size_t{1}, requests.size(), "2-bit: one refresh request");
    runner.expectTrue(requests[0] == papyrix::XtcPageRenderer::RefreshRequest::GrayscaleBase,
                      "2-bit: requires unconditional grayscale base refresh");
  }

  {
    bool allByteTransformsMatch = true;
    for (unsigned int plane1 = 0; plane1 <= 0xFF; ++plane1) {
      for (unsigned int plane2 = 0; plane2 <= 0xFF; ++plane2) {
        uint8_t expectedBase = 0;
        uint8_t expectedLsb = 0;
        uint8_t expectedMsb = 0;
        for (uint8_t bit = 0; bit < 8; ++bit) {
          const uint8_t mask = static_cast<uint8_t>(0x80U >> bit);
          const uint8_t value = static_cast<uint8_t>(((plane1 & mask) ? 2 : 0) | ((plane2 & mask) ? 1 : 0));
          if (value == 0) expectedBase |= mask;
          if (value == 1) expectedLsb |= mask;
          if (value == 1 || value == 2) expectedMsb |= mask;
        }
        allByteTransformsMatch &=
            xtc::xthBaseByte(static_cast<uint8_t>(plane1), static_cast<uint8_t>(plane2)) == expectedBase &&
            xtc::xthLsbByte(static_cast<uint8_t>(plane1), static_cast<uint8_t>(plane2)) == expectedLsb &&
            xtc::xthMsbByte(static_cast<uint8_t>(plane1), static_cast<uint8_t>(plane2)) == expectedMsb;
      }
    }
    runner.expectTrue(allByteTransformsMatch, "XTH byte transforms match pixel values");
  }

  {
    SdMan.reset();
    constexpr uint16_t width = 480;
    constexpr uint16_t height = 800;
    const size_t planeSize = xtc::xthPlaneSize(width, height);
    std::vector<uint8_t> pixels(planeSize * 2);
    memset(pixels.data(), 0x3C, planeSize);
    memset(pixels.data() + planeSize, 0x5A, planeSize);
    SdMan.registerFile("/full.xtch", buildXtcFile(xtc::XTCH_MAGIC, pixels, width, height));

    EInkDisplay fullDisplay(0, 0, 0, 0, 0, 0);
    GfxRenderer fullGfx(fullDisplay);
    fullGfx.begin();
    papyrix::XtcPageRenderer fullRenderer(fullGfx);
    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/full.xtch") == xtc::XtcError::OK, "full-size 2-bit: parser opens");
    std::vector<uint8_t> base;
    testSetLargestFreeBlock(10240);
    const auto result = fullRenderer.render(parser, 0, [&](papyrix::XtcPageRenderer::RefreshRequest request) {
      if (request == papyrix::XtcPageRenderer::RefreshRequest::GrayscaleBase) {
        base.assign(fullGfx.getFrameBuffer(), fullGfx.getFrameBuffer() + fullGfx.getBufferSize());
      }
    });
    testResetLargestFreeBlock();
    runner.expectTrue(result == papyrix::XtcPageRenderer::RenderResult::Success,
                      "full-size 2-bit: renders with 8KB scratch");
    runner.expectTrue(!base.empty() && base[0] == 0x81, "native base uses ~(p1 | p2)");
    runner.expectTrue(!fullDisplay.grayscaleLsb().empty() && fullDisplay.grayscaleLsb()[0] == 0x42,
                      "native LSB uses ~p1 & p2");
    runner.expectTrue(!fullDisplay.grayscaleMsb().empty() && fullDisplay.grayscaleMsb()[0] == 0x66,
                      "native MSB uses p1 ^ p2");
    runner.expectTrue(fullGfx.getFrameBuffer()[0] == 0x81, "final base is reconstructed");
  }

  {
    constexpr uint16_t width = 8;
    constexpr uint16_t height = 8;
    std::vector<uint8_t> pixels(xtc::xthBitmapSize(width, height), 0);
    pixels[xtc::xthPlaneSize(width, height) + 7] = 0x80;

    SdMan.reset();
    SdMan.registerFile("/inverted.xtch", buildXtcFile(xtc::XTCH_MAGIC, pixels, width, height));
    EInkDisplay invertedDisplay(0, 0, 0, 0, 0, 0);
    GfxRenderer invertedGfx(invertedDisplay);
    invertedGfx.begin();
    invertedGfx.setOrientation(GfxRenderer::PortraitInverted);
    papyrix::XtcPageRenderer invertedRenderer(invertedGfx);
    xtc::XtcParser invertedParser;
    invertedParser.open("/inverted.xtch");
    const auto invertedResult = invertedRenderer.render(
        invertedParser, 0, [](papyrix::XtcPageRenderer::RefreshRequest) {});
    runner.expectTrue(invertedResult == papyrix::XtcPageRenderer::RenderResult::Success,
                      "generic inverted: renders");
    runner.expectTrue(isPhysicalBlack(invertedGfx.getFrameBuffer(), EInkDisplay::DISPLAY_WIDTH_BYTES, 799, 0),
                      "generic inverted: maps logical origin");

    SdMan.reset();
    SdMan.registerFile("/x3.xtch", buildXtcFile(xtc::XTCH_MAGIC, pixels, width, height));
    EInkDisplay x3Display(0, 0, 0, 0, 0, 0);
    x3Display.setDisplayX3();
    GfxRenderer x3Gfx(x3Display);
    x3Gfx.begin();
    papyrix::XtcPageRenderer x3Renderer(x3Gfx);
    xtc::XtcParser x3Parser;
    x3Parser.open("/x3.xtch");
    const auto x3Result = x3Renderer.render(x3Parser, 0, [](papyrix::XtcPageRenderer::RefreshRequest) {});
    runner.expectTrue(x3Result == papyrix::XtcPageRenderer::RenderResult::Success, "generic X3: renders");
    runner.expectTrue(isPhysicalBlack(x3Gfx.getFrameBuffer(), EInkDisplay::X3_DISPLAY_WIDTH_BYTES, 0, 527),
                      "generic X3: maps logical origin");
  }

  {
    SdMan.reset();
    SdMan.registerFile("/mixed.xtc", buildMixedDimensionXtc());

    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/mixed.xtc") == xtc::XtcError::OK, "mixed dimensions: parser opens");
    std::vector<papyrix::XtcPageRenderer::RefreshRequest> requests;
    const auto result = renderer.render(
        parser, 1, [&requests](papyrix::XtcPageRenderer::RefreshRequest request) { requests.push_back(request); });
    runner.expectTrue(result == papyrix::XtcPageRenderer::RenderResult::Success,
                      "mixed dimensions: requested page renders");
    runner.expectEq(size_t{1}, requests.size(), "mixed dimensions: one refresh request");
  }

  {
    constexpr size_t firstPassFailureLimit = 120;
    constexpr size_t lsbFailureLimit = 170;
    constexpr size_t msbFailureLimit = 220;
    constexpr size_t finalBaseFailureLimit = 275;
    const size_t limits[] = {firstPassFailureLimit, lsbFailureLimit, msbFailureLimit, finalBaseFailureLimit};

    for (const size_t limit : limits) {
      SdMan.reset();
      SdMan.registerFile("/read-failure.xtch", buildXtcFile(xtc::XTCH_MAGIC, std::vector<uint8_t>(16, 0)));
      SdMan.setReadLimit(limit);
      EInkDisplay failureDisplay(0, 0, 0, 0, 0, 0);
      GfxRenderer failureGfx(failureDisplay);
      failureGfx.begin();
      papyrix::XtcPageRenderer failureRenderer(failureGfx);
      xtc::XtcParser parser;
      runner.expectTrue(parser.open("/read-failure.xtch") == xtc::XtcError::OK,
                        "pass failure: parser opens");
      std::vector<papyrix::XtcPageRenderer::RefreshRequest> requests;
      const auto result = failureRenderer.render(
          parser, 0,
          [&requests](papyrix::XtcPageRenderer::RefreshRequest request) { requests.push_back(request); });
      runner.expectTrue(result == papyrix::XtcPageRenderer::RenderResult::PageLoadFailed,
                        "pass failure: returns page load failure");
      if (limit == firstPassFailureLimit) {
        runner.expectTrue(requests.empty() && failureDisplay.cleanupCount() == 0,
                          "initial pass failure: no refresh or cleanup");
      } else {
        runner.expectTrue(requests.size() == 1 && failureDisplay.cleanupCount() == 1,
                          "later pass failure: refresh followed by cleanup");
      }
      const int expectedGrayDisplays = limit == finalBaseFailureLimit ? 1 : 0;
      runner.expectEq(expectedGrayDisplays, failureDisplay.displayGrayCount(),
                      "pass failure: grayscale display count");
    }
    SdMan.reset();
  }

  {
    SdMan.reset();
    std::string file = buildXtcFile(xtc::XTCH_MAGIC, std::vector<uint8_t>(16, 0));
    constexpr size_t pageTableOffset = sizeof(xtc::XtcHeader) + 128 + 64;
    auto* data = reinterpret_cast<uint8_t*>(&file[0]);
    auto* entry = reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset);
    auto* pageHeader = reinterpret_cast<xtc::XtgPageHeader*>(data + entry->dataOffset);
    pageHeader->width = 16;
    SdMan.registerFile("/malformed.xtch", file);

    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/malformed.xtch") == xtc::XtcError::OK, "malformed stream: parser opens");
    std::vector<papyrix::XtcPageRenderer::RefreshRequest> requests;
    const auto result = renderer.render(
        parser, 0, [&requests](papyrix::XtcPageRenderer::RefreshRequest request) { requests.push_back(request); });
    runner.expectTrue(result == papyrix::XtcPageRenderer::RenderResult::PageLoadFailed,
                      "malformed stream: page load fails safely");
    runner.expectTrue(requests.empty(), "malformed stream: no refresh requested");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
