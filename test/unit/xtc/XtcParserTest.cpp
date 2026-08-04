#include "test_utils.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <Xtc/XtcParser.h>
#include <Xtc/XtcTypes.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static_assert(sizeof(xtc::XtcHeader) == 56);
static_assert(sizeof(xtc::XtcHeader::chapterOffset) == sizeof(uint64_t));
static_assert(offsetof(xtc::XtcHeader, hasChapters) == 0x0B);
static_assert(offsetof(xtc::XtcHeader, pageTableOffset) == 0x18);
static_assert(offsetof(xtc::XtcHeader, chapterOffset) == 0x30);

// Helper: build a multi-page XTC file in memory
static std::string buildMultiPageXtc(uint16_t width, uint16_t height, uint16_t pageCount,
                                     uint32_t magic = xtc::XTC_MAGIC) {
  constexpr size_t headerSize = sizeof(xtc::XtcHeader);
  constexpr size_t titleSize = 128;
  constexpr size_t authorSize = 64;
  const size_t pageTableOffset = headerSize + titleSize + authorSize;
  constexpr size_t pageEntrySize = sizeof(xtc::PageTableEntry);
  const size_t pageDataStart = pageTableOffset + pageEntrySize * pageCount;

  const bool is2bit = (magic == xtc::XTCH_MAGIC);
  const size_t bitmapSize = is2bit ? xtc::xthBitmapSize(width, height) : xtc::xtgBitmapSize(width, height);
  const size_t pageDataSize = sizeof(xtc::XtgPageHeader) + bitmapSize;
  const size_t totalSize = pageDataStart + pageDataSize * pageCount;

  std::string buf(totalSize, '\0');
  auto* data = reinterpret_cast<uint8_t*>(&buf[0]);

  auto* hdr = reinterpret_cast<xtc::XtcHeader*>(data);
  hdr->magic = magic;
  hdr->versionMajor = 1;
  hdr->versionMinor = 0;
  hdr->pageCount = pageCount;
  hdr->hasMetadata = 1;
  hdr->metadataOffset = headerSize;
  hdr->pageTableOffset = pageTableOffset;
  hdr->dataOffset = pageDataStart;

  const char* title = "Multi Page Book";
  memcpy(data + headerSize, title, strlen(title));

  const uint32_t xtgMagic = is2bit ? xtc::XTH_MAGIC : xtc::XTG_MAGIC;

  for (uint16_t i = 0; i < pageCount; i++) {
    const size_t pageOffset = pageDataStart + pageDataSize * i;

    auto* pte = reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset + pageEntrySize * i);
    pte->dataOffset = pageOffset;
    pte->dataSize = static_cast<uint32_t>(pageDataSize);
    pte->width = width;
    pte->height = height;

    auto* pageHdr = reinterpret_cast<xtc::XtgPageHeader*>(data + pageOffset);
    pageHdr->magic = xtgMagic;
    pageHdr->width = width;
    pageHdr->height = height;
    pageHdr->colorMode = 0;
    pageHdr->compression = 0;
    pageHdr->dataSize = static_cast<uint32_t>(bitmapSize);

    // Fill bitmap with page-specific pattern for verification
    uint8_t* bitmap = data + pageOffset + sizeof(xtc::XtgPageHeader);
    memset(bitmap, static_cast<uint8_t>(i & 0xFF), bitmapSize);
  }

  return buf;
}

static std::string buildLegacyPageTableXtc() {
  constexpr size_t pageTableOffset = offsetof(xtc::XtcHeader, chapterOffset);
  constexpr size_t pageDataOffset = pageTableOffset + sizeof(xtc::PageTableEntry);
  constexpr uint16_t width = 8;
  constexpr uint16_t height = 1;
  constexpr size_t bitmapSize = 1;
  constexpr size_t pageDataSize = sizeof(xtc::XtgPageHeader) + bitmapSize;

  std::string file(pageDataOffset + pageDataSize, '\0');
  auto* data = reinterpret_cast<uint8_t*>(&file[0]);
  auto* header = reinterpret_cast<xtc::XtcHeader*>(data);
  header->magic = xtc::XTC_MAGIC;
  header->versionMajor = 1;
  header->pageCount = 1;
  header->pageTableOffset = pageTableOffset;
  header->dataOffset = pageDataOffset;

  auto* entry = reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset);
  entry->dataOffset = pageDataOffset;
  entry->dataSize = pageDataSize;
  entry->width = width;
  entry->height = height;

  auto* pageHeader = reinterpret_cast<xtc::XtgPageHeader*>(data + pageDataOffset);
  pageHeader->magic = xtc::XTG_MAGIC;
  pageHeader->width = width;
  pageHeader->height = height;
  pageHeader->dataSize = bitmapSize;
  return file;
}

static std::string buildRelocatedMetadataXtc() {
  const std::string original = buildMultiPageXtc(8, 1, 1);
  constexpr size_t oldPageTableOffset = sizeof(xtc::XtcHeader) + 128 + 64;
  constexpr size_t oldPageDataOffset = oldPageTableOffset + sizeof(xtc::PageTableEntry);
  constexpr size_t metadataOffset = sizeof(xtc::XtcHeader) + 32;
  constexpr size_t newPageTableOffset = metadataOffset + 128 + 64;
  constexpr size_t newPageDataOffset = newPageTableOffset + sizeof(xtc::PageTableEntry);

  std::string file(original.size() + (newPageDataOffset - oldPageDataOffset), '\0');
  memcpy(&file[0], original.data(), sizeof(xtc::XtcHeader));
  memcpy(&file[newPageTableOffset], original.data() + oldPageTableOffset, sizeof(xtc::PageTableEntry));
  memcpy(&file[newPageDataOffset], original.data() + oldPageDataOffset, original.size() - oldPageDataOffset);

  auto* data = reinterpret_cast<uint8_t*>(&file[0]);
  auto* header = reinterpret_cast<xtc::XtcHeader*>(data);
  header->metadataOffset = metadataOffset;
  header->pageTableOffset = newPageTableOffset;
  header->dataOffset = newPageDataOffset;
  auto* entry = reinterpret_cast<xtc::PageTableEntry*>(data + newPageTableOffset);
  entry->dataOffset = newPageDataOffset;
  memcpy(data + metadataOffset, "Relocated Title", 15);
  memcpy(data + metadataOffset + 128, "Relocated Author", 16);
  return file;
}

static std::string buildChapterXtc(size_t chapterCount, bool hasMetadata = true) {
  constexpr size_t headerSize = sizeof(xtc::XtcHeader);
  constexpr size_t metadataSize = 256;
  constexpr size_t chapterSize = 96;
  constexpr uint16_t width = 8;
  constexpr uint16_t height = 1;
  const size_t chapterOffset = headerSize + (hasMetadata ? metadataSize : 0);
  const size_t pageTableOffset = chapterOffset + chapterCount * chapterSize;
  const size_t pageDataOffset = pageTableOffset + sizeof(xtc::PageTableEntry);
  const size_t bitmapSize = xtc::xtgBitmapSize(width, height);
  const size_t pageDataSize = sizeof(xtc::XtgPageHeader) + bitmapSize;

  std::string file(pageDataOffset + pageDataSize, '\0');
  auto* data = reinterpret_cast<uint8_t*>(&file[0]);
  auto* header = reinterpret_cast<xtc::XtcHeader*>(data);
  header->magic = xtc::XTC_MAGIC;
  header->versionMajor = 1;
  header->pageCount = 1;
  header->hasMetadata = hasMetadata ? 1 : 0;
  header->hasChapters = 1;
  header->metadataOffset = hasMetadata ? headerSize : 0;
  header->pageTableOffset = pageTableOffset;
  header->dataOffset = pageDataOffset;
  header->chapterOffset = chapterOffset;

  if (hasMetadata) {
    memcpy(data + headerSize, "Chapter Book", 12);
    const uint16_t declaredCount = static_cast<uint16_t>(std::min<size_t>(chapterCount, UINT16_MAX));
    memcpy(data + headerSize + 196, &declaredCount, sizeof(declaredCount));
  }

  for (size_t i = 0; i < chapterCount; i++) {
    uint8_t* chapter = data + chapterOffset + i * chapterSize;
    snprintf(reinterpret_cast<char*>(chapter), 80, "Chapter %zu", i + 1);
    const uint16_t page = 1;
    memcpy(chapter + 0x50, &page, sizeof(page));
    memcpy(chapter + 0x52, &page, sizeof(page));
  }

  auto* entry = reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset);
  entry->dataOffset = pageDataOffset;
  entry->dataSize = pageDataSize;
  entry->width = width;
  entry->height = height;

  auto* pageHeader = reinterpret_cast<xtc::XtgPageHeader*>(data + pageDataOffset);
  pageHeader->magic = xtc::XTG_MAGIC;
  pageHeader->width = width;
  pageHeader->height = height;
  pageHeader->dataSize = bitmapSize;
  return file;
}

int main() {
  TestUtils::TestRunner runner("XtcParser Tests");

  runner.expectEq(size_t{16}, xtc::xthPlaneSize(8, 9), "XTH 8x9 plane pads every column");
  runner.expectEq(size_t{48}, xtc::xthBitmapSize(8, 17), "XTH 8x17 bitmap has two padded planes");

  {
    SdMan.clearFiles();
    SdMan.registerFile("/legacy_table.xtc", buildLegacyPageTableXtc());
    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/legacy_table.xtc") == xtc::XtcError::OK,
                      "legacy_table: table at 0x30 remains supported");
  }

  {
    SdMan.clearFiles();
    std::string data = buildLegacyPageTableXtc();
    auto* header = reinterpret_cast<xtc::XtcHeader*>(&data[0]);
    header->pageTableOffset = offsetof(xtc::XtcHeader, chapterOffset) - 1;
    SdMan.registerFile("/table_in_header.xtc", data);
    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/table_in_header.xtc") == xtc::XtcError::CORRUPTED_HEADER,
                      "table_bounds: rejects table before legacy header boundary");
  }

  {
    SdMan.clearFiles();
    SdMan.registerFile("/relocated_metadata.xtc", buildRelocatedMetadataXtc());
    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/relocated_metadata.xtc") == xtc::XtcError::OK,
                      "metadata_offset: parser opens");
    runner.expectEq(std::string("Relocated Title"), parser.getTitle(),
                    "metadata_offset: title uses declared offset");
    runner.expectEq(std::string("Relocated Author"), parser.getAuthor(),
                    "metadata_offset: author follows relocated title");
  }

  {
    constexpr uint16_t width = 2;
    constexpr uint16_t height = 9;
    const size_t planeSize = xtc::xthPlaneSize(width, height);
    std::vector<uint8_t> bitmap(planeSize * 2, 0);
    const size_t byteOffset = xtc::xthColumnBytes(height) + 1;
    bitmap[byteOffset] = 0x80;
    runner.expectEq(static_cast<uint8_t>(2),
                    xtc::xthPixelValue(bitmap.data(), bitmap.data() + planeSize, width, height, 0, 8),
                    "XTH pixel lookup reaches padded final row");
  }

  // Test 1: getPageInfo lazy loading - multi-page random access
  {
    SdMan.clearFiles();
    const uint16_t w = 16, h = 8;
    std::string xtcData = buildMultiPageXtc(w, h, 5);
    SdMan.registerFile("/multi.xtc", xtcData);

    xtc::XtcParser parser;
    auto err = parser.open("/multi.xtc");
    runner.expectTrue(err == xtc::XtcError::OK, "lazy_multi: opens successfully");
    runner.expectEq(static_cast<uint16_t>(5), parser.getPageCount(), "lazy_multi: page count");

    // Access pages in non-sequential order
    xtc::PageInfo info;

    bool ok4 = parser.getPageInfo(4, info);
    runner.expectTrue(ok4, "lazy_multi: page 4 accessible");
    runner.expectEq(w, info.width, "lazy_multi: page 4 width");
    runner.expectEq(h, info.height, "lazy_multi: page 4 height");

    bool ok0 = parser.getPageInfo(0, info);
    runner.expectTrue(ok0, "lazy_multi: page 0 after page 4");
    runner.expectEq(w, info.width, "lazy_multi: page 0 width");

    bool ok2 = parser.getPageInfo(2, info);
    runner.expectTrue(ok2, "lazy_multi: page 2 random access");
    runner.expectEq(w, info.width, "lazy_multi: page 2 width");

    parser.close();
  }

  // Test 2: getPageInfo out of range
  {
    SdMan.clearFiles();
    std::string xtcData = buildMultiPageXtc(16, 8, 3);
    SdMan.registerFile("/small.xtc", xtcData);

    xtc::XtcParser parser;
    parser.open("/small.xtc");

    xtc::PageInfo info;
    bool ok = parser.getPageInfo(3, info);
    runner.expectFalse(ok, "out_of_range: page 3 of 3 fails");

    ok = parser.getPageInfo(100, info);
    runner.expectFalse(ok, "out_of_range: page 100 fails");

    parser.close();
  }

  // Test 3: loadPage with lazy page table
  {
    SdMan.clearFiles();
    const uint16_t w = 8, h = 1;
    std::string xtcData = buildMultiPageXtc(w, h, 3);
    SdMan.registerFile("/load.xtc", xtcData);

    xtc::XtcParser parser;
    parser.open("/load.xtc");

    const size_t bitmapSize = ((w + 7) / 8) * h;
    std::vector<uint8_t> buf(bitmapSize + 256);

    // Load page 2 (pattern byte = 2)
    size_t bytes = parser.loadPage(2, buf.data(), buf.size());
    runner.expectTrue(bytes > 0, "load_lazy: page 2 loads data");
    runner.expectEq(static_cast<uint8_t>(2), buf[0], "load_lazy: page 2 pattern correct");

    // Load page 0 (pattern byte = 0)
    bytes = parser.loadPage(0, buf.data(), buf.size());
    runner.expectTrue(bytes > 0, "load_lazy: page 0 loads data");
    runner.expectEq(static_cast<uint8_t>(0), buf[0], "load_lazy: page 0 pattern correct");

    // Out of range
    bytes = parser.loadPage(3, buf.data(), buf.size());
    runner.expectEq(static_cast<size_t>(0), bytes, "load_lazy: out of range returns 0");

    parser.close();
  }

  // Test 4: MAX_XTC_PAGE_COUNT validation
  {
    SdMan.clearFiles();

    // Build a header-only file with page count exceeding the limit
    constexpr size_t headerSize = sizeof(xtc::XtcHeader);
    constexpr size_t titleSize = 128;
    constexpr size_t authorSize = 64;
    const size_t minSize = headerSize + titleSize + authorSize + sizeof(xtc::PageTableEntry);
    std::string buf(minSize, '\0');
    auto* data = reinterpret_cast<uint8_t*>(&buf[0]);

    auto* hdr = reinterpret_cast<xtc::XtcHeader*>(data);
    hdr->magic = xtc::XTC_MAGIC;
    hdr->versionMajor = 1;
    hdr->versionMinor = 0;
    hdr->pageCount = xtc::MAX_XTC_PAGE_COUNT + 1;  // Exceeds limit
    hdr->hasMetadata = 1;
    hdr->pageTableOffset = headerSize + titleSize + authorSize;
    hdr->dataOffset = hdr->pageTableOffset + sizeof(xtc::PageTableEntry);

    SdMan.registerFile("/too_many.xtc", buf);

    xtc::XtcParser parser;
    auto err = parser.open("/too_many.xtc");
    runner.expectTrue(err == xtc::XtcError::CORRUPTED_HEADER, "max_pages: rejects page count > MAX");

    parser.close();
  }

  // Test 5: MAX_XTC_PAGE_COUNT at boundary (exactly at limit is OK)
  {
    SdMan.clearFiles();

    // Build a minimal valid header with exactly MAX_XTC_PAGE_COUNT pages
    // We don't need actual page data for this - just enough for the header check to pass
    // and the page table validation to have enough size
    constexpr size_t headerSize = sizeof(xtc::XtcHeader);
    constexpr size_t titleSize = 128;
    constexpr size_t authorSize = 64;
    const size_t pageTableOffset = headerSize + titleSize + authorSize;
    const size_t pageTableSize = static_cast<size_t>(xtc::MAX_XTC_PAGE_COUNT) * sizeof(xtc::PageTableEntry);
    const size_t pageDataStart = pageTableOffset + pageTableSize;

    // Need at least one valid page entry with bitmap for readPageTable to succeed
    const uint16_t w = 8, h = 1;
    const size_t bitmapSize = 1;
    const size_t pageDataSize = sizeof(xtc::XtgPageHeader) + bitmapSize;
    const size_t totalSize = pageDataStart + pageDataSize;

    std::string buf(totalSize, '\0');
    auto* data = reinterpret_cast<uint8_t*>(&buf[0]);

    auto* hdr = reinterpret_cast<xtc::XtcHeader*>(data);
    hdr->magic = xtc::XTC_MAGIC;
    hdr->versionMajor = 1;
    hdr->versionMinor = 0;
    hdr->pageCount = xtc::MAX_XTC_PAGE_COUNT;
    hdr->hasMetadata = 1;
    hdr->pageTableOffset = pageTableOffset;
    hdr->dataOffset = pageDataStart;

    // Fill first page entry so readPageTable can read default dimensions
    auto* pte = reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset);
    pte->dataOffset = pageDataStart;
    pte->dataSize = static_cast<uint32_t>(pageDataSize);
    pte->width = w;
    pte->height = h;

    auto* pageHdr = reinterpret_cast<xtc::XtgPageHeader*>(data + pageDataStart);
    pageHdr->magic = xtc::XTG_MAGIC;
    pageHdr->width = w;
    pageHdr->height = h;
    pageHdr->dataSize = static_cast<uint32_t>(bitmapSize);

    SdMan.registerFile("/max_pages.xtc", buf);

    xtc::XtcParser parser;
    auto err = parser.open("/max_pages.xtc");
    runner.expectTrue(err == xtc::XtcError::OK, "max_boundary: accepts exactly MAX page count");
    runner.expectEq(xtc::MAX_XTC_PAGE_COUNT, parser.getPageCount(), "max_boundary: correct count");

    parser.close();
  }

  // Test 6: Page table extends beyond file size
  {
    SdMan.clearFiles();

    constexpr size_t headerSize = sizeof(xtc::XtcHeader);
    constexpr size_t titleSize = 128;
    constexpr size_t authorSize = 64;
    const size_t pageTableOffset = headerSize + titleSize + authorSize;

    // File is too short for the declared page table
    const size_t totalSize = pageTableOffset + 4;  // Way too small for even 1 page entry (16 bytes)
    std::string buf(totalSize, '\0');
    auto* data = reinterpret_cast<uint8_t*>(&buf[0]);

    auto* hdr = reinterpret_cast<xtc::XtcHeader*>(data);
    hdr->magic = xtc::XTC_MAGIC;
    hdr->versionMajor = 1;
    hdr->versionMinor = 0;
    hdr->pageCount = 10;  // Claims 10 pages but file is tiny
    hdr->hasMetadata = 1;
    hdr->pageTableOffset = pageTableOffset;
    hdr->dataOffset = pageTableOffset + 160;

    SdMan.registerFile("/truncated.xtc", buf);

    xtc::XtcParser parser;
    auto err = parser.open("/truncated.xtc");
    runner.expectTrue(err == xtc::XtcError::CORRUPTED_HEADER, "truncated_table: rejects file too small for page table");

    parser.close();
  }

  // Test 7: Default dimensions from first page
  {
    SdMan.clearFiles();
    const uint16_t w = 480, h = 800;
    std::string xtcData = buildMultiPageXtc(w, h, 2);
    SdMan.registerFile("/dims.xtc", xtcData);

    xtc::XtcParser parser;
    parser.open("/dims.xtc");

    runner.expectEq(w, parser.getWidth(), "default_dims: width from first page");
    runner.expectEq(h, parser.getHeight(), "default_dims: height from first page");
    runner.expectEq(static_cast<uint8_t>(1), parser.getBitDepth(), "default_dims: 1-bit depth");

    parser.close();
  }

  // Test 8: 2-bit XTCH lazy loading
  {
    SdMan.clearFiles();
    const uint16_t w = 8, h = 8;
    std::string xtcData = buildMultiPageXtc(w, h, 3, xtc::XTCH_MAGIC);
    SdMan.registerFile("/multi.xtch", xtcData);

    xtc::XtcParser parser;
    auto err = parser.open("/multi.xtch");
    runner.expectTrue(err == xtc::XtcError::OK, "xtch_lazy: opens XTCH");
    runner.expectEq(static_cast<uint8_t>(2), parser.getBitDepth(), "xtch_lazy: 2-bit depth");

    xtc::PageInfo info;
    bool ok = parser.getPageInfo(1, info);
    runner.expectTrue(ok, "xtch_lazy: page 1 accessible");
    runner.expectEq(static_cast<uint8_t>(2), info.bitDepth, "xtch_lazy: page info has 2-bit depth");
    runner.expectEq(w, info.width, "xtch_lazy: page 1 width");
    runner.expectEq(h, info.height, "xtch_lazy: page 1 height");

    parser.close();
  }

  // Test 9: loadPageStreaming with lazy page table
  {
    SdMan.clearFiles();
    const uint16_t w = 8, h = 1;
    std::string xtcData = buildMultiPageXtc(w, h, 2);
    SdMan.registerFile("/stream.xtc", xtcData);

    xtc::XtcParser parser;
    parser.open("/stream.xtc");

    std::vector<uint8_t> collected;
    auto err = parser.loadPageStreaming(1, [&](const uint8_t* data, size_t size, size_t offset) {
      (void)offset;
      collected.insert(collected.end(), data, data + size);
    });
    runner.expectTrue(err == xtc::XtcError::OK, "streaming_lazy: page 1 streams OK");
    runner.expectTrue(collected.size() > 0, "streaming_lazy: received data");

    // Out of range
    err = parser.loadPageStreaming(2, [](const uint8_t*, size_t, size_t) {});
    runner.expectTrue(err == xtc::XtcError::PAGE_OUT_OF_RANGE, "streaming_lazy: out of range error");

    parser.close();
  }

  // Test 10: page table and embedded dimensions must agree
  {
    SdMan.clearFiles();
    std::string data = buildMultiPageXtc(16, 8, 1);
    constexpr size_t pageTableOffset = sizeof(xtc::XtcHeader) + 128 + 64;
    auto* entry = reinterpret_cast<xtc::PageTableEntry*>(&data[pageTableOffset]);
    auto* pageHeader = reinterpret_cast<xtc::XtgPageHeader*>(&data[entry->dataOffset]);
    pageHeader->width = 8;
    SdMan.registerFile("/mismatch.xtc", data);

    xtc::XtcParser parser;
    parser.open("/mismatch.xtc");
    std::vector<uint8_t> buffer(32);
    runner.expectEq(size_t{0}, parser.loadPage(0, buffer.data(), buffer.size()),
                    "page_validation: rejects table/header dimension mismatch");
  }

  // Test 11: compressed pages are unsupported
  {
    SdMan.clearFiles();
    std::string data = buildMultiPageXtc(8, 8, 1);
    constexpr size_t pageTableOffset = sizeof(xtc::XtcHeader) + 128 + 64;
    auto* entry = reinterpret_cast<xtc::PageTableEntry*>(&data[pageTableOffset]);
    auto* pageHeader = reinterpret_cast<xtc::XtgPageHeader*>(&data[entry->dataOffset]);
    pageHeader->compression = 1;
    SdMan.registerFile("/compressed.xtc", data);

    xtc::XtcParser parser;
    parser.open("/compressed.xtc");
    std::vector<uint8_t> buffer(32);
    runner.expectEq(size_t{0}, parser.loadPage(0, buffer.data(), buffer.size()),
                    "page_validation: rejects unsupported compression");
  }

  // Test 12: embedded and table data sizes are validated
  {
    SdMan.clearFiles();
    std::string data = buildMultiPageXtc(8, 8, 1);
    constexpr size_t pageTableOffset = sizeof(xtc::XtcHeader) + 128 + 64;
    auto* entry = reinterpret_cast<xtc::PageTableEntry*>(&data[pageTableOffset]);
    auto* pageHeader = reinterpret_cast<xtc::XtgPageHeader*>(&data[entry->dataOffset]);
    pageHeader->dataSize++;
    SdMan.registerFile("/wrong_size.xtc", data);

    xtc::XtcParser parser;
    parser.open("/wrong_size.xtc");
    std::vector<uint8_t> buffer(32);
    runner.expectEq(size_t{0}, parser.loadPage(0, buffer.data(), buffer.size()),
                    "page_validation: rejects incorrect embedded data size");

    data = buildMultiPageXtc(8, 8, 1);
    entry = reinterpret_cast<xtc::PageTableEntry*>(&data[pageTableOffset]);
    entry->dataSize = sizeof(xtc::XtgPageHeader);
    SdMan.registerFile("/short_entry.xtc", data);
    parser.close();
    parser.open("/short_entry.xtc");
    runner.expectEq(size_t{0}, parser.loadPage(0, buffer.data(), buffer.size()),
                    "page_validation: rejects truncated page entry");
  }

  // Test 13: invalid streaming arguments fail before allocation
  {
    SdMan.clearFiles();
    SdMan.registerFile("/zero_chunk.xtc", buildMultiPageXtc(8, 8, 1));
    xtc::XtcParser parser;
    parser.open("/zero_chunk.xtc");
    const auto err = parser.loadPageStreaming(0, [](const uint8_t*, size_t, size_t) {}, 0);
    runner.expectTrue(err == xtc::XtcError::CORRUPTED_HEADER, "streaming: rejects zero chunk size");
  }

  // Test 14: chapter parsing is bounded, lazy, and close releases capacity
  {
    SdMan.clearFiles();
    SdMan.registerFile("/chapters.xtc", buildChapterXtc(501));
    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/chapters.xtc") == xtc::XtcError::OK, "chapters: parser opens");
    runner.expectEq(size_t{0}, parser.getLoadedChapterCountForTest(), "chapters: not loaded during open");
    runner.expectTrue(parser.hasChapters(), "chapters: lazy load succeeds");
    runner.expectEq(size_t{500}, parser.getChapters().size(), "chapters: parser caps entries at 500");
    parser.close();
    runner.expectEq(size_t{0}, parser.getChapters().capacity(), "chapters: close releases vector capacity");
  }

  {
    SdMan.clearFiles();
    SdMan.registerFile("/no_metadata_chapters.xtc", buildChapterXtc(2, false));
    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/no_metadata_chapters.xtc") == xtc::XtcError::OK,
                      "metadata_less_chapters: parser opens");
    runner.expectTrue(parser.hasChapters(), "metadata_less_chapters: chapters load");
    runner.expectEq(size_t{2}, parser.getChapters().size(), "metadata_less_chapters: count");
  }

  {
    SdMan.clearFiles();
    SdMan.registerFile("/low_heap_chapters.xtc", buildChapterXtc(500));
    testSetLargestFreeBlock(1024);
    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/low_heap_chapters.xtc") == xtc::XtcError::OK,
                      "low_heap_chapters: optional TOC does not block open");
    runner.expectFalse(parser.hasChapters(), "low_heap_chapters: TOC omitted");
    runner.expectEq(size_t{0}, parser.getChapters().size(), "low_heap_chapters: no partial TOC retained");
    testResetLargestFreeBlock();
    runner.expectFalse(parser.hasChapters(), "low_heap_chapters: failed lazy load is not retried");
  }

  return runner.allPassed() ? 0 : 1;
}
