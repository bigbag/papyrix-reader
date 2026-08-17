#include "test_utils.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <XtcCoverHelper.h>
#include <Xtc/XtcParser.h>
#include <Xtc/XtcTypes.h>
#include <platform_stubs.h>

#include <cstring>
#include <string>
#include <vector>

// Helper: build a minimal valid 1-bit XTC file in memory
static std::string buildXtcFile1Bit(uint16_t width, uint16_t height, const std::vector<uint8_t>& pixelData) {
  // Layout:
  //   0x00: XtcHeader (56 bytes)
  //   0x38: title (128 bytes, null-terminated)
  //   0xB8: author (64 bytes)
  //   0xF8: page table (16 bytes per page)
  //   0x108: page data (XtgPageHeader + bitmap)

  constexpr size_t headerSize = sizeof(xtc::XtcHeader);       // 56
  constexpr size_t titleSize = 128;
  constexpr size_t authorSize = 64;
  constexpr size_t pageTableOffset = headerSize + titleSize + authorSize;  // 0xF8
  constexpr size_t pageEntrySize = sizeof(xtc::PageTableEntry);           // 16
  const size_t pageDataOffset = pageTableOffset + pageEntrySize;

  // XTG page header (22 bytes)
  const size_t bitmapSize = ((width + 7) / 8) * static_cast<size_t>(height);
  const size_t pageDataSize = sizeof(xtc::XtgPageHeader) + bitmapSize;
  const size_t totalSize = pageDataOffset + pageDataSize;

  std::string buf(totalSize, '\0');
  auto* data = reinterpret_cast<uint8_t*>(&buf[0]);

  // XtcHeader
  auto* hdr = reinterpret_cast<xtc::XtcHeader*>(data);
  hdr->magic = xtc::XTC_MAGIC;
  hdr->versionMajor = 1;
  hdr->versionMinor = 0;
  hdr->pageCount = 1;
  hdr->hasMetadata = 1;
  hdr->pageTableOffset = pageTableOffset;
  hdr->dataOffset = pageDataOffset;

  // Title
  const char* title = "Test Book";
  memcpy(data + headerSize, title, strlen(title));

  // Page table entry
  auto* pte = reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset);
  pte->dataOffset = pageDataOffset;
  pte->dataSize = static_cast<uint32_t>(pageDataSize);
  pte->width = width;
  pte->height = height;

  // XTG page header
  auto* pageHdr = reinterpret_cast<xtc::XtgPageHeader*>(data + pageDataOffset);
  pageHdr->magic = xtc::XTG_MAGIC;
  pageHdr->width = width;
  pageHdr->height = height;
  pageHdr->colorMode = 0;
  pageHdr->compression = 0;
  pageHdr->dataSize = static_cast<uint32_t>(bitmapSize);

  // Bitmap data
  const size_t bitmapOffset = pageDataOffset + sizeof(xtc::XtgPageHeader);
  const size_t toCopy = std::min(pixelData.size(), bitmapSize);
  if (toCopy > 0) {
    memcpy(data + bitmapOffset, pixelData.data(), toCopy);
  }
  // Remaining bytes stay 0 (white in XTC: 0=black, 1=white... actually 0 bits)

  return buf;
}

// Helper: build a minimal valid 2-bit XTCH file in memory
static std::string buildXtcFile2Bit(uint16_t width, uint16_t height, const std::vector<uint8_t>& pixelData) {
  constexpr size_t headerSize = sizeof(xtc::XtcHeader);
  constexpr size_t titleSize = 128;
  constexpr size_t authorSize = 64;
  constexpr size_t pageTableOffset = headerSize + titleSize + authorSize;
  constexpr size_t pageEntrySize = sizeof(xtc::PageTableEntry);
  const size_t pageDataOffset = pageTableOffset + pageEntrySize;

  const size_t bitmapSize = xtc::xthBitmapSize(width, height);
  const size_t pageDataSize = sizeof(xtc::XtgPageHeader) + bitmapSize;
  const size_t totalSize = pageDataOffset + pageDataSize;

  std::string buf(totalSize, '\0');
  auto* data = reinterpret_cast<uint8_t*>(&buf[0]);

  auto* hdr = reinterpret_cast<xtc::XtcHeader*>(data);
  hdr->magic = xtc::XTCH_MAGIC;
  hdr->versionMajor = 1;
  hdr->versionMinor = 0;
  hdr->pageCount = 1;
  hdr->hasMetadata = 1;
  hdr->pageTableOffset = pageTableOffset;
  hdr->dataOffset = pageDataOffset;

  const char* title = "Test Book 2bit";
  memcpy(data + headerSize, title, strlen(title));

  auto* pte = reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset);
  pte->dataOffset = pageDataOffset;
  pte->dataSize = static_cast<uint32_t>(pageDataSize);
  pte->width = width;
  pte->height = height;

  auto* pageHdr = reinterpret_cast<xtc::XtgPageHeader*>(data + pageDataOffset);
  pageHdr->magic = xtc::XTH_MAGIC;
  pageHdr->width = width;
  pageHdr->height = height;
  pageHdr->colorMode = 0;
  pageHdr->compression = 0;
  pageHdr->dataSize = static_cast<uint32_t>(bitmapSize);

  const size_t bitmapOffset = pageDataOffset + sizeof(xtc::XtgPageHeader);
  const size_t toCopy = std::min(pixelData.size(), bitmapSize);
  if (toCopy > 0) {
    memcpy(data + bitmapOffset, pixelData.data(), toCopy);
  }

  return buf;
}

static void setXthPixel(std::vector<uint8_t>& bitmap, uint16_t width, uint16_t height, uint16_t x, uint16_t y,
                        uint8_t value) {
  const size_t planeSize = xtc::xthPlaneSize(width, height);
  const size_t byteOffset = static_cast<size_t>(width - 1 - x) * xtc::xthColumnBytes(height) + y / 8;
  const uint8_t mask = static_cast<uint8_t>(1u << (7 - y % 8));
  if (value & 2) bitmap[byteOffset] |= mask;
  if (value & 1) bitmap[planeSize + byteOffset] |= mask;
}

// Helper: parse BMP header fields from raw data
struct BmpInfo {
  char magic[2];
  uint32_t fileSize;
  uint32_t dataOffset;
  uint32_t dibSize;
  int32_t width;
  int32_t height;
  uint16_t bitsPerPixel;
  uint32_t imageSize;
};

static BmpInfo parseBmpHeader(const std::string& data) {
  BmpInfo info{};
  if (data.size() < 62) return info;
  const auto* d = reinterpret_cast<const uint8_t*>(data.data());
  info.magic[0] = static_cast<char>(d[0]);
  info.magic[1] = static_cast<char>(d[1]);
  memcpy(&info.fileSize, d + 2, 4);
  memcpy(&info.dataOffset, d + 10, 4);
  memcpy(&info.dibSize, d + 14, 4);
  memcpy(&info.width, d + 18, 4);
  memcpy(&info.height, d + 22, 4);
  memcpy(&info.bitsPerPixel, d + 28, 2);
  memcpy(&info.imageSize, d + 34, 4);
  return info;
}

int main() {
  TestUtils::TestRunner runner("XtcCoverHelper Tests");

  // ---- Test: 1-bit cover generation with small image ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    const uint16_t w = 16, h = 8;
    // 1-bit: 2 bytes per row, 8 rows = 16 bytes
    // All 0xFF = all white pixels
    std::vector<uint8_t> pixels(2 * 8, 0xFF);
    std::string xtcData = buildXtcFile1Bit(w, h, pixels);
    SdMan.registerFile("/test.xtc", xtcData);

    xtc::XtcParser parser;
    auto err = parser.open("/test.xtc");
    runner.expectTrue(err == xtc::XtcError::OK, "1-bit: parser opens successfully");
    runner.expectEq(static_cast<uint16_t>(1), parser.getPageCount(), "1-bit: page count is 1");
    runner.expectEq(static_cast<uint8_t>(1), parser.getBitDepth(), "1-bit: bit depth is 1");

    const bool result =
        xtc::generateCoverBmpFromParser(parser, "/cache/cover.bmp") == xtc::CoverResult::Generated;
    runner.expectTrue(result, "1-bit: cover generation succeeds");

    std::string bmpData = SdMan.getWrittenData("/cache/cover.bmp");
    runner.expectTrue(bmpData.size() > 62, "1-bit: BMP data has header");

    BmpInfo bmp = parseBmpHeader(bmpData);
    runner.expectEq('B', bmp.magic[0], "1-bit: BMP magic B");
    runner.expectEq('M', bmp.magic[1], "1-bit: BMP magic M");
    runner.expectEq(static_cast<int32_t>(w), bmp.width, "1-bit: BMP width matches");
    runner.expectEq(-static_cast<int32_t>(h), bmp.height, "1-bit: BMP height negative (top-down)");
    runner.expectEq(static_cast<uint16_t>(1), bmp.bitsPerPixel, "1-bit: BMP bits per pixel is 1");
    runner.expectEq(static_cast<uint32_t>(40), bmp.dibSize, "1-bit: DIB header is BITMAPINFOHEADER");
    runner.expectEq(static_cast<uint32_t>(14 + 40 + 8), bmp.dataOffset, "1-bit: data offset = header + dib + palette");

    // Verify file size: header(14) + dib(40) + palette(8) + image
    const uint32_t rowSize = ((w + 31) / 32) * 4;
    const uint32_t expectedImageSize = rowSize * h;
    const uint32_t expectedFileSize = 14 + 40 + 8 + expectedImageSize;
    runner.expectEq(expectedFileSize, bmp.fileSize, "1-bit: BMP file size correct");
    runner.expectEq(expectedFileSize, static_cast<uint32_t>(bmpData.size()), "1-bit: actual data size matches");

    parser.close();
  }

  // ---- Test: 2-bit cover generation ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    const uint16_t w = 8, h = 8;
    // 2-bit: bitmapSize = ((8*8+7)/8)*2 = 16 bytes (8 per plane)
    // All zeros = all white (pixel value 0 = white in cover helper threshold)
    std::vector<uint8_t> pixels(16, 0x00);
    std::string xtcData = buildXtcFile2Bit(w, h, pixels);
    SdMan.registerFile("/test.xtch", xtcData);

    xtc::XtcParser parser;
    auto err = parser.open("/test.xtch");
    runner.expectTrue(err == xtc::XtcError::OK, "2-bit: parser opens successfully");
    runner.expectEq(static_cast<uint8_t>(2), parser.getBitDepth(), "2-bit: bit depth is 2");

    const bool result =
        xtc::generateCoverBmpFromParser(parser, "/cache/cover2.bmp") == xtc::CoverResult::Generated;
    runner.expectTrue(result, "2-bit: cover generation succeeds");

    std::string bmpData = SdMan.getWrittenData("/cache/cover2.bmp");
    runner.expectTrue(bmpData.size() > 62, "2-bit: BMP data has header");

    BmpInfo bmp = parseBmpHeader(bmpData);
    runner.expectEq('B', bmp.magic[0], "2-bit: BMP magic B");
    runner.expectEq('M', bmp.magic[1], "2-bit: BMP magic M");
    runner.expectEq(static_cast<int32_t>(w), bmp.width, "2-bit: BMP width matches");
    runner.expectEq(static_cast<uint16_t>(1), bmp.bitsPerPixel, "2-bit: output is 1-bit BMP");

    parser.close();
  }

  // ---- Test: 1-bit pixel data roundtrip ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    const uint16_t w = 8, h = 2;
    // Row 0: 0xAA = 10101010 (alternating black/white)
    // Row 1: 0x55 = 01010101
    std::vector<uint8_t> pixels = {0xAA, 0x55};
    std::string xtcData = buildXtcFile1Bit(w, h, pixels);
    SdMan.registerFile("/test_px.xtc", xtcData);

    xtc::XtcParser parser;
    parser.open("/test_px.xtc");

    xtc::generateCoverBmpFromParser(parser, "/cache/px.bmp");
    std::string bmpData = SdMan.getWrittenData("/cache/px.bmp");

    // Data starts at offset 62 (14+40+8)
    // Row size for 8px wide = ((8+31)/32)*4 = 4 bytes (padded)
    runner.expectTrue(bmpData.size() >= 62 + 8, "pixel: BMP large enough for 2 rows");

    // Row 0 should be 0xAA followed by 3 padding bytes
    runner.expectEq(static_cast<uint8_t>(0xAA), static_cast<uint8_t>(bmpData[62]),
                    "pixel: row 0 data matches source");
    // Row 1 should be 0x55
    runner.expectEq(static_cast<uint8_t>(0x55), static_cast<uint8_t>(bmpData[66]),
                    "pixel: row 1 data matches source");

    parser.close();
  }

  // ---- Test: 2-bit pixel conversion ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    // 8x8 image, 2-bit mode
    // Two planes of 8 bytes each. Column-major, right-to-left, 8 vertical pixels per byte
    // All bits set in plane1 = all pixels have bit1=1, bit2 depends on plane2
    // If plane1 all 0xFF and plane2 all 0x00: pixelValue = (1<<1)|0 = 2 >= 1 → black
    const uint16_t w = 8, h = 8;
    std::vector<uint8_t> pixels(16, 0x00);
    // Set plane1 (first 8 bytes) to all 1s → all pixels have bit1=1 → value >= 1 → black
    for (int i = 0; i < 8; i++) pixels[i] = 0xFF;
    // Plane2 stays 0

    std::string xtcData = buildXtcFile2Bit(w, h, pixels);
    SdMan.registerFile("/test_2b.xtch", xtcData);

    xtc::XtcParser parser;
    parser.open("/test_2b.xtch");

    xtc::generateCoverBmpFromParser(parser, "/cache/2b.bmp");
    std::string bmpData = SdMan.getWrittenData("/cache/2b.bmp");

    // All pixels should be black (0x00 in BMP 1-bit)
    // Data at offset 62, row size = 4 bytes (8px width padded to 32-bit)
    runner.expectTrue(bmpData.size() >= 62 + 32, "2-bit pixel: BMP large enough");
    // First byte of row 0: all 8 pixels black = 0x00
    runner.expectEq(static_cast<uint8_t>(0x00), static_cast<uint8_t>(bmpData[62]),
                    "2-bit pixel: all-dark pixels convert to black");

    parser.close();
  }

  // ---- Test: all-white 2-bit image ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    const uint16_t w = 8, h = 8;
    // Both planes all zeros → pixelValue = 0 → white
    std::vector<uint8_t> pixels(16, 0x00);

    std::string xtcData = buildXtcFile2Bit(w, h, pixels);
    SdMan.registerFile("/test_2bw.xtch", xtcData);

    xtc::XtcParser parser;
    parser.open("/test_2bw.xtch");

    xtc::generateCoverBmpFromParser(parser, "/cache/2bw.bmp");
    std::string bmpData = SdMan.getWrittenData("/cache/2bw.bmp");

    runner.expectTrue(bmpData.size() >= 62 + 4, "2-bit white: BMP large enough");
    // All pixels white = 0xFF in BMP 1-bit
    runner.expectEq(static_cast<uint8_t>(0xFF), static_cast<uint8_t>(bmpData[62]),
                    "2-bit white: all-zero pixels convert to white");

    parser.close();
  }

  // ---- Test: BMP palette (black=0, white=1) ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    const uint16_t w = 8, h = 1;
    std::vector<uint8_t> pixels(1, 0x00);
    std::string xtcData = buildXtcFile1Bit(w, h, pixels);
    SdMan.registerFile("/test_pal.xtc", xtcData);

    xtc::XtcParser parser;
    parser.open("/test_pal.xtc");
    xtc::generateCoverBmpFromParser(parser, "/cache/pal.bmp");
    std::string bmpData = SdMan.getWrittenData("/cache/pal.bmp");

    // Palette starts at offset 54 (14+40)
    // Color 0 (black): B=0, G=0, R=0, A=0
    runner.expectEq(static_cast<uint8_t>(0x00), static_cast<uint8_t>(bmpData[54]), "palette: color 0 blue=0");
    runner.expectEq(static_cast<uint8_t>(0x00), static_cast<uint8_t>(bmpData[55]), "palette: color 0 green=0");
    runner.expectEq(static_cast<uint8_t>(0x00), static_cast<uint8_t>(bmpData[56]), "palette: color 0 red=0");
    // Color 1 (white): B=FF, G=FF, R=FF, A=0
    runner.expectEq(static_cast<uint8_t>(0xFF), static_cast<uint8_t>(bmpData[58]), "palette: color 1 blue=FF");
    runner.expectEq(static_cast<uint8_t>(0xFF), static_cast<uint8_t>(bmpData[59]), "palette: color 1 green=FF");
    runner.expectEq(static_cast<uint8_t>(0xFF), static_cast<uint8_t>(bmpData[60]), "palette: color 1 red=FF");

    parser.close();
  }

  // ---- Test: row padding to 4-byte boundary ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    // 10px wide → 2 bytes per row in source, but BMP needs 4-byte alignment = 4 bytes per row
    const uint16_t w = 10, h = 2;
    std::vector<uint8_t> pixels = {0xFF, 0xC0, 0xFF, 0xC0};  // 2 bytes per row, 2 rows
    std::string xtcData = buildXtcFile1Bit(w, h, pixels);
    SdMan.registerFile("/test_pad.xtc", xtcData);

    xtc::XtcParser parser;
    parser.open("/test_pad.xtc");
    xtc::generateCoverBmpFromParser(parser, "/cache/pad.bmp");
    std::string bmpData = SdMan.getWrittenData("/cache/pad.bmp");

    BmpInfo bmp = parseBmpHeader(bmpData);
    const uint32_t expectedRowSize = ((w + 31) / 32) * 4;  // 4 bytes
    runner.expectEq(static_cast<uint32_t>(4), expectedRowSize, "padding: row size is 4 bytes");

    // Total image size = 4 * 2 = 8
    runner.expectEq(expectedRowSize * h, bmp.imageSize, "padding: image size accounts for padding");

    // Verify padding bytes are 0
    // Row 0: bytes 62,63 = data, bytes 64,65 = padding
    runner.expectEq(static_cast<uint8_t>(0x00), static_cast<uint8_t>(bmpData[64]),
                    "padding: pad byte 0 of row 0 is zero");
    runner.expectEq(static_cast<uint8_t>(0x00), static_cast<uint8_t>(bmpData[65]),
                    "padding: pad byte 1 of row 0 is zero");

    parser.close();
  }

  // ---- Test: non-byte-aligned XTH column heights ----
  for (const uint16_t h : {uint16_t{9}, uint16_t{17}}) {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    constexpr uint16_t w = 8;
    std::vector<uint8_t> pixels(xtc::xthBitmapSize(w, h), 0);
    setXthPixel(pixels, w, h, 0, h - 1, 2);
    SdMan.registerFile("/unaligned.xtch", buildXtcFile2Bit(w, h, pixels));

    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/unaligned.xtch") == xtc::XtcError::OK, "unaligned XTH: parser opens");
    runner.expectTrue(
        xtc::generateCoverBmpFromParser(parser, "/cache/unaligned.bmp") == xtc::CoverResult::Generated,
        "unaligned XTH: cover generation succeeds");

    const std::string bmpData = SdMan.getWrittenData("/cache/unaligned.bmp");
    constexpr size_t dataOffset = 62;
    constexpr size_t rowSize = 4;
    const size_t bottomRow = dataOffset + static_cast<size_t>(h - 1) * rowSize;
    const bool hasBottomRow = bmpData.size() > bottomRow;
    runner.expectTrue(hasBottomRow, "unaligned XTH: BMP contains final row");
    if (hasBottomRow) {
      runner.expectEq(static_cast<uint8_t>(0),
                      static_cast<uint8_t>(static_cast<uint8_t>(bmpData[bottomRow]) & 0x80),
                      "unaligned XTH: final-row pixel is decoded");
    }
  }

  // ---- Test: cancellation does not publish a partial cover ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    constexpr uint16_t w = 16;
    constexpr uint16_t h = 16;
    std::vector<uint8_t> pixels((w + 7) / 8 * h, 0xFF);
    SdMan.registerFile("/abort.xtc", buildXtcFile1Bit(w, h, pixels));

    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/abort.xtc") == xtc::XtcError::OK, "abort: parser opens");
    int checks = 0;
    const bool result = xtc::generateCoverBmpFromParser(
        parser, "/cache/aborted-cover.bmp", [&checks]() { return ++checks >= 3; }) ==
        xtc::CoverResult::TransientFailure;
    runner.expectTrue(result, "abort: cover generation stops");
    runner.expectFalse(SdMan.exists("/cache/aborted-cover.bmp"), "abort: partial cover is not published");
    runner.expectFalse(SdMan.exists("/cache/aborted-cover.bmp.part"), "abort: partial cover is removed");
  }

  // ---- Test: full-size 480x800 XTCH cover succeeds under constrained heap ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    testResetLargestFreeBlock();

    constexpr uint16_t w = 480, h = 800;
    std::vector<uint8_t> pixels(xtc::xthBitmapSize(w, h), 0);
    for (uint16_t x = 0; x < w; x += 2) setXthPixel(pixels, w, h, x, 0, 2);
    SdMan.registerFile("/full.xtch", buildXtcFile2Bit(w, h, pixels));

    xtc::XtcParser parser;
    runner.expectTrue(parser.open("/full.xtch") == xtc::XtcError::OK, "full XTCH: parser opens");
    // Old code needs a 96000-byte page buffer; 80% of 12000 is 9600 -> impossible.
    // The band-transpose scratch is 16*8*60 = 7680 bytes and must pass.
    testSetLargestFreeBlock(12000);
    runner.expectTrue(
        xtc::generateCoverBmpFromParser(parser, "/cache/full.bmp") == xtc::CoverResult::Generated,
        "full XTCH: cover generated with bounded memory");
    testResetLargestFreeBlock();

    const std::string bmpData = SdMan.getWrittenData("/cache/full.bmp");
    const uint32_t expectedSize = 62 + ((480 + 31) / 32) * 4 * 800;
    runner.expectTrue(bmpData.size() == expectedSize, "full XTCH: BMP size correct");
    if (bmpData.size() == expectedSize) {
      // Reference decode of row 0 via xthPixelValue (even x black, odd x white).
      const size_t rowBytes = (w + 7) / 8;
      const size_t bmpRow = ((w + 31) / 32) * 4;
      std::vector<uint8_t> expectRow(rowBytes, 0xFF);
      for (uint16_t x = 0; x < w; x++) {
        const uint8_t v =
            xtc::xthPixelValue(pixels.data(), pixels.data() + xtc::xthPlaneSize(w, h), w, h, x, 0);
        if (v >= 2) expectRow[x >> 3] &= static_cast<uint8_t>(~(1u << (7 - (x & 7))));
      }
      bool rowMatches = true;
      for (size_t b = 0; b < rowBytes; b++) {
        if (static_cast<uint8_t>(bmpData[62 + b]) != expectRow[b]) rowMatches = false;
      }
      for (size_t p = rowBytes; p < bmpRow; p++) {
        if (static_cast<uint8_t>(bmpData[62 + p]) != 0) rowMatches = false;
      }
      runner.expectTrue(rowMatches, "full XTCH: row 0 matches reference decode");
    }
    parser.close();
  }

  // ---- Test: band-transpose correctness vs reference decoder (24x17, odd height) ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    constexpr uint16_t w = 24, h = 17;
    std::vector<uint8_t> pixels(xtc::xthBitmapSize(w, h), 0);
    uint32_t seed = 12345;
    for (size_t i = 0; i < pixels.size(); i++) {
      seed = seed * 1103515245 + 12345;
      pixels[i] = (seed >> 16) & 0xFF;
    }
    SdMan.registerFile("/rand.xtch", buildXtcFile2Bit(w, h, pixels));

    xtc::XtcParser parser;
    parser.open("/rand.xtch");
    runner.expectTrue(
        xtc::generateCoverBmpFromParser(parser, "/cache/rand.bmp") == xtc::CoverResult::Generated,
        "rand XTCH: generated");

    const std::string bmp = SdMan.getWrittenData("/cache/rand.bmp");
    const size_t rowBytes = (w + 7) / 8;
    const size_t bmpRow = ((w + 31) / 32) * 4;
    bool allMatch = bmp.size() == 62 + bmpRow * h;
    std::vector<uint8_t> expectRow(rowBytes, 0xFF);
    for (uint16_t y = 0; y < h && allMatch; y++) {
      std::fill(expectRow.begin(), expectRow.end(), 0xFF);
      for (uint16_t x = 0; x < w; x++) {
        const uint8_t v =
            xtc::xthPixelValue(pixels.data(), pixels.data() + xtc::xthPlaneSize(w, h), w, h, x, y);
        if (v >= 2) expectRow[x >> 3] &= static_cast<uint8_t>(~(1u << (7 - (x & 7))));
      }
      for (size_t b = 0; b < rowBytes; b++) {
        if (static_cast<uint8_t>(bmp[62 + y * bmpRow + b]) != expectRow[b]) allMatch = false;
      }
      for (size_t p = rowBytes; p < bmpRow; p++) {
        if (static_cast<uint8_t>(bmp[62 + y * bmpRow + p]) != 0) allMatch = false;
      }
    }
    runner.expectTrue(allMatch, "rand XTCH: every row matches reference decode, padding zero");
    parser.close();
  }

  // ---- Test: full-size 1-bit XTC cover under tight heap ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    constexpr uint16_t w = 480, h = 800;
    std::vector<uint8_t> pixels((w + 7) / 8 * h, 0xA5);
    SdMan.registerFile("/full1.xtc", buildXtcFile1Bit(w, h, pixels));

    xtc::XtcParser parser;
    parser.open("/full1.xtc");
    testSetLargestFreeBlock(2000);  // 80% = 1600; only chunks <= 1024 are allowed
    runner.expectTrue(
        xtc::generateCoverBmpFromParser(parser, "/cache/full1.bmp") == xtc::CoverResult::Generated,
        "full 1-bit: generated with row streaming");
    testResetLargestFreeBlock();
    runner.expectEq(static_cast<uint8_t>(0xA5), static_cast<uint8_t>(SdMan.getWrittenData("/cache/full1.bmp")[62]),
                    "full 1-bit: first row preserved");
    parser.close();
  }

  // ---- Test: transient heap refusal returns TransientFailure without artifacts ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    constexpr uint16_t w = 100, h = 200;  // band scratch = 16*8*16 = 2048 bytes
    std::vector<uint8_t> pixels(xtc::xthBitmapSize(w, h), 0);
    SdMan.registerFile("/mem.xtch", buildXtcFile2Bit(w, h, pixels));

    xtc::XtcParser parser;
    parser.open("/mem.xtch");
    testSetLargestFreeBlock(2000);  // 80% = 1600 < 2048
    runner.expectTrue(
        xtc::generateCoverBmpFromParser(parser, "/cache/mem.bmp") == xtc::CoverResult::TransientFailure,
        "memory gate: TransientFailure");
    testResetLargestFreeBlock();
    runner.expectFalse(SdMan.exists("/cache/mem.bmp"), "memory gate: no cover published");
    runner.expectFalse(SdMan.exists("/cache/mem.bmp.part"), "memory gate: no part left");
    runner.expectTrue(
        xtc::generateCoverBmpFromParser(parser, "/cache/mem.bmp") == xtc::CoverResult::Generated,
        "memory gate: retry after recovery succeeds");
    parser.close();
  }

  // ---- Test: corrupt page 0 is InvalidFile ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    std::string data = buildXtcFile1Bit(16, 8, std::vector<uint8_t>(16, 0xFF));
    const size_t pageDataOffset = 56 + 128 + 64 + 16;
    data[pageDataOffset] = 'X';
    data[pageDataOffset + 1] = 'X';  // corrupt XTG page magic
    SdMan.registerFile("/bad.xtc", data);

    xtc::XtcParser parser;
    parser.open("/bad.xtc");
    runner.expectTrue(
        xtc::generateCoverBmpFromParser(parser, "/cache/bad.bmp") == xtc::CoverResult::InvalidFile,
        "corrupt page: InvalidFile");
    parser.close();
  }

  // ---- Test: gray-level threshold (50% luminance midpoint) ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    // 4x1 image: x0=white(0), x1=light gray(1), x2=dark gray(2), x3=black(3).
    // Light gray stays white; dark gray and black render black.
    constexpr uint16_t w = 4, h = 1;
    std::vector<uint8_t> pixels(xtc::xthBitmapSize(w, h), 0);
    setXthPixel(pixels, w, h, 1, 0, 1);
    setXthPixel(pixels, w, h, 2, 0, 2);
    setXthPixel(pixels, w, h, 3, 0, 3);
    SdMan.registerFile("/gray.xtch", buildXtcFile2Bit(w, h, pixels));

    xtc::XtcParser parser;
    parser.open("/gray.xtch");
    runner.expectTrue(
        xtc::generateCoverBmpFromParser(parser, "/cache/gray.bmp") == xtc::CoverResult::Generated,
        "gray: cover generated");
    const std::string bmp = SdMan.getWrittenData("/cache/gray.bmp");
    runner.expectTrue(bmp.size() >= 63, "gray: BMP large enough");
    // bit 7=x0 white(1), bit 6=x1 light gray -> white(1), bit 5=x2 dark -> black(0), bit 4=x3 black(0)
    // bit7..4 = x0..x3 (white, white, black, black); bits beyond width are white pad
    runner.expectEq(static_cast<uint8_t>(0xCF), static_cast<uint8_t>(bmp[62]),
                    "gray: white+light stay white, dark+black render black");
    parser.close();
  }

  // ---- Test: 2-bit cancellation mid-stream leaves no artifacts ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();

    constexpr uint16_t w = 64, h = 128;
    std::vector<uint8_t> pixels(xtc::xthBitmapSize(w, h), 0);
    SdMan.registerFile("/abort2.xtch", buildXtcFile2Bit(w, h, pixels));

    xtc::XtcParser parser;
    parser.open("/abort2.xtch");
    int checks = 0;
    runner.expectTrue(
        xtc::generateCoverBmpFromParser(parser, "/cache/abort2.bmp", [&checks]() { return ++checks >= 2; }) ==
            xtc::CoverResult::TransientFailure,
        "2-bit abort: TransientFailure");
    runner.expectFalse(SdMan.exists("/cache/abort2.bmp"), "2-bit abort: no cover");
    runner.expectFalse(SdMan.exists("/cache/abort2.bmp.part"), "2-bit abort: no part");
    parser.close();
  }

  return runner.allPassed() ? 0 : 1;
}
