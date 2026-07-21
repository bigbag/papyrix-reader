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
  header->headerSize = headerSize;
  header->pageTableOffset = pageTableOffset;
  header->dataOffset = pageDataOffset;
  header->titleOffset = headerSize;

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

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
