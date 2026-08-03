#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Xtc/XtcParser.h>
#include <Xtc/XtcTypes.h>
#include <XtcPageRenderer.h>

#include <cstring>
#include <string>
#include <vector>

#include "test_utils.h"

namespace {

std::string buildXtcFile(uint32_t magic, const std::vector<uint8_t>& pixels) {
  constexpr uint16_t width = 8;
  constexpr uint16_t height = 8;
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
