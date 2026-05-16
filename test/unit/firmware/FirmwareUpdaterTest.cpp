#include "test_utils.h"

#include <cstring>
#include <vector>

#include "SDCardManager.h"
#include "esp_partition.h"

#define PAPYRIX_FIRMWARE_FILE "/firmware.bin"
#include "FirmwareUpdater.h"

using namespace papyrix;

// Build a minimal valid ESP32 firmware image:
// - 24-byte header (magic=0xE9, 1 segment, hash_appended flag)
// - 8-byte segment header + N bytes of segment data
// - Padding to 16-byte alignment with XOR checksum at the last pad byte
// - 32-byte SHA256 trailer (if hashAppended)
static std::vector<uint8_t> buildValidFirmware(size_t segDataSize, bool hashAppended = true) {
  constexpr uint8_t MAGIC = 0xE9;
  constexpr uint8_t CHECKSUM_SEED = 0xEF;
  constexpr size_t HEADER_SIZE = 24;
  constexpr size_t SEG_HDR_SIZE = 8;

  std::vector<uint8_t> img;
  img.resize(HEADER_SIZE, 0);
  img[0] = MAGIC;
  img[1] = 1;  // 1 segment
  img[23] = hashAppended ? 1 : 0;

  // Segment header: 4-byte load address (0) + 4-byte length
  uint8_t segHdr[SEG_HDR_SIZE] = {};
  uint32_t dataLen = static_cast<uint32_t>(segDataSize);
  memcpy(segHdr + 4, &dataLen, 4);
  img.insert(img.end(), segHdr, segHdr + SEG_HDR_SIZE);

  // Segment data (pattern)
  for (size_t i = 0; i < segDataSize; i++) {
    img.push_back(static_cast<uint8_t>(i & 0xFF));
  }

  // XOR checksum over segment data
  uint8_t xorAccum = CHECKSUM_SEED;
  size_t dataStart = HEADER_SIZE + SEG_HDR_SIZE;
  for (size_t i = dataStart; i < dataStart + segDataSize; i++) {
    xorAccum ^= img[i];
  }

  // Pad to 16-byte alignment; checksum goes in last pad byte
  size_t pos = img.size();
  size_t padEnd = (pos + 16) & ~static_cast<size_t>(15);
  size_t padLen = padEnd - pos;
  for (size_t i = 0; i < padLen - 1; i++) {
    img.push_back(0);
  }
  img.push_back(xorAccum);

  if (hashAppended) {
    // Compute "SHA256" using same algorithm as our mock (XOR-fold)
    uint8_t hash[32] = {};
    for (size_t i = 0; i < img.size(); i++) {
      hash[i % 32] ^= img[i];
    }
    img.insert(img.end(), hash, hash + 32);
  }

  return img;
}

// Build a firmware that's big enough to pass MIN_FIRMWARE_SIZE (64KB)
static std::vector<uint8_t> buildMinSizeFirmware(bool hashAppended = true) {
  return buildValidFirmware(64 * 1024, hashAppended);
}

int main() {
  TestUtils::TestRunner runner("FirmwareUpdater");

  // ============================================================
  // findFirmwareFile
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    bool found = FW_UPDATER.findFirmwareFile("/nonexistent.bin");
    runner.expectFalse(found, "findFirmwareFile: nonexistent returns false");
    runner.expectFalse(FW_UPDATER.isFirmwareAvailable(), "findFirmwareFile: not available after miss");
  }

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware();
    SdMan.setFileData("/test_fw.bin", data);
    bool found = FW_UPDATER.findFirmwareFile("/test_fw.bin");
    runner.expectTrue(found, "findFirmwareFile: existing returns true");
    runner.expectTrue(FW_UPDATER.isFirmwareAvailable(), "findFirmwareFile: available after hit");
  }

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware();
    SdMan.setFileData(PAPYRIX_FIRMWARE_FILE, data);
    bool found = FW_UPDATER.findFirmwareFile();
    runner.expectTrue(found, "findFirmwareFile: default path finds file");
  }

  // ============================================================
  // reset
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware();
    SdMan.setFileData("/test_fw.bin", data);
    FW_UPDATER.findFirmwareFile("/test_fw.bin");
    runner.expectTrue(FW_UPDATER.isFirmwareAvailable(), "reset: available before reset");
    FW_UPDATER.reset();
    runner.expectFalse(FW_UPDATER.isFirmwareAvailable(), "reset: not available after reset");
    const auto& p = FW_UPDATER.progress();
    runner.expectEq(static_cast<int>(FirmwareUpdatePhase::Idle), static_cast<int>(p.phase),
                    "reset: phase back to Idle");
  }

  // ============================================================
  // beginUpdate — validation: too small
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    // Valid header but below MIN_FIRMWARE_SIZE
    auto data = buildValidFirmware(100);
    SdMan.setFileData("/small.bin", data);
    FW_UPDATER.findFirmwareFile("/small.bin");
    bool ok = FW_UPDATER.beginUpdate();
    runner.expectFalse(ok, "beginUpdate: too small returns false");
    runner.expectEq(static_cast<int>(FirmwareUpdateError::TooSmall),
                    static_cast<int>(FW_UPDATER.progress().error), "beginUpdate: TooSmall error");
  }

  // ============================================================
  // beginUpdate — validation: bad magic
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware();
    data[0] = 0x00;  // Corrupt magic
    SdMan.setFileData("/bad_magic.bin", data);
    FW_UPDATER.findFirmwareFile("/bad_magic.bin");
    bool ok = FW_UPDATER.beginUpdate();
    runner.expectFalse(ok, "beginUpdate: bad magic returns false");
    runner.expectEq(static_cast<int>(FirmwareUpdateError::BadMagic),
                    static_cast<int>(FW_UPDATER.progress().error), "beginUpdate: BadMagic error");
  }

  // ============================================================
  // beginUpdate — validation: bad checksum
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware(false);  // no SHA trailer
    // Corrupt a data byte to break XOR checksum
    data[50] ^= 0xFF;
    SdMan.setFileData("/bad_cksum.bin", data);
    FW_UPDATER.findFirmwareFile("/bad_cksum.bin");
    bool ok = FW_UPDATER.beginUpdate();
    runner.expectFalse(ok, "beginUpdate: bad checksum returns false");
    runner.expectEq(static_cast<int>(FirmwareUpdateError::BadChecksum),
                    static_cast<int>(FW_UPDATER.progress().error), "beginUpdate: BadChecksum error");
  }

  // ============================================================
  // beginUpdate — validation: bad SHA
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware(true);
    // Corrupt the SHA trailer (last 32 bytes)
    data[data.size() - 1] ^= 0xFF;
    SdMan.setFileData("/bad_sha.bin", data);
    FW_UPDATER.findFirmwareFile("/bad_sha.bin");
    bool ok = FW_UPDATER.beginUpdate();
    runner.expectFalse(ok, "beginUpdate: bad SHA returns false");
    runner.expectEq(static_cast<int>(FirmwareUpdateError::BadSha),
                    static_cast<int>(FW_UPDATER.progress().error), "beginUpdate: BadSha error");
  }

  // ============================================================
  // beginUpdate — no partition
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    ESP_PARTITION.nextUpdatePartition_ = nullptr;
    auto data = buildMinSizeFirmware();
    SdMan.setFileData("/fw.bin", data);
    FW_UPDATER.findFirmwareFile("/fw.bin");
    bool ok = FW_UPDATER.beginUpdate();
    runner.expectFalse(ok, "beginUpdate: no partition returns false");
    runner.expectEq(static_cast<int>(FirmwareUpdateError::NoPartition),
                    static_cast<int>(FW_UPDATER.progress().error), "beginUpdate: NoPartition error");
  }

  // ============================================================
  // beginUpdate — not available
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    bool ok = FW_UPDATER.beginUpdate();
    runner.expectFalse(ok, "beginUpdate: not available returns false");
    runner.expectEq(static_cast<int>(FirmwareUpdateError::NoFile),
                    static_cast<int>(FW_UPDATER.progress().error), "beginUpdate: NoFile error");
  }

  // ============================================================
  // beginUpdate — success + pump full flow
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware();
    SdMan.setFileData(PAPYRIX_FIRMWARE_FILE, data);
    FW_UPDATER.findFirmwareFile();

    bool ok = FW_UPDATER.beginUpdate();
    runner.expectTrue(ok, "full flow: beginUpdate success");
    runner.expectEq(static_cast<int>(FirmwareUpdatePhase::Flashing),
                    static_cast<int>(FW_UPDATER.progress().phase), "full flow: phase is Flashing");

    int pumps = 0;
    while (FW_UPDATER.pump()) { pumps++; }

    const auto& p = FW_UPDATER.progress();
    runner.expectEq(static_cast<int>(FirmwareUpdatePhase::Complete), static_cast<int>(p.phase),
                    "full flow: Complete");
    runner.expectEq(static_cast<uint32_t>(data.size()), p.bytesFlashed, "full flow: all bytes flashed");
    runner.expectEq(static_cast<uint32_t>(data.size()), p.totalBytes, "full flow: totalBytes matches");

    // Verify flash content matches original firmware
    bool match = true;
    for (size_t i = 0; i < data.size() && match; i++) {
      if (ESP_PARTITION.flashData_[i] != data[i]) match = false;
    }
    runner.expectTrue(match, "full flow: flash data integrity ok");
    runner.expectTrue(ESP_PARTITION.eraseCount_ > 0, "full flow: erase was called");
    runner.expectTrue(ESP_PARTITION.writeCount_ > 0, "full flow: write was called");
  }

  // ============================================================
  // pump — erase failure
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware();
    SdMan.setFileData(PAPYRIX_FIRMWARE_FILE, data);
    FW_UPDATER.findFirmwareFile();
    FW_UPDATER.beginUpdate();
    ESP_PARTITION.eraseOk_ = false;

    bool more = FW_UPDATER.pump();
    runner.expectFalse(more, "pump: erase fail returns false");
    runner.expectEq(static_cast<int>(FirmwareUpdateError::FlashError),
                    static_cast<int>(FW_UPDATER.progress().error), "pump: erase fail → FlashError");
  }

  // ============================================================
  // pump — write failure
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware();
    SdMan.setFileData(PAPYRIX_FIRMWARE_FILE, data);
    FW_UPDATER.findFirmwareFile();
    FW_UPDATER.beginUpdate();
    ESP_PARTITION.writeOk_ = false;

    bool more = FW_UPDATER.pump();
    runner.expectFalse(more, "pump: write fail returns false");
    runner.expectEq(static_cast<int>(FirmwareUpdateError::FlashError),
                    static_cast<int>(FW_UPDATER.progress().error), "pump: write fail → FlashError");
  }

  // ============================================================
  // abort
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware();
    SdMan.setFileData(PAPYRIX_FIRMWARE_FILE, data);
    FW_UPDATER.findFirmwareFile();
    FW_UPDATER.beginUpdate();

    FW_UPDATER.pump();  // advance one chunk
    runner.expectTrue(FW_UPDATER.progress().bytesFlashed > 0, "abort: pumped some bytes");

    FW_UPDATER.abort();
    bool more = FW_UPDATER.pump();
    runner.expectFalse(more, "abort: pump after abort returns false");
    runner.expectEq(static_cast<int>(FirmwareUpdateError::Aborted),
                    static_cast<int>(FW_UPDATER.progress().error), "abort: Aborted error");
  }

  // ============================================================
  // pump — not Flashing
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    bool ok = FW_UPDATER.pump();
    runner.expectFalse(ok, "pump: not Flashing returns false");
  }

  // ============================================================
  // progress — initial state
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    const auto& p = FW_UPDATER.progress();
    runner.expectEq(static_cast<int>(FirmwareUpdatePhase::Idle), static_cast<int>(p.phase),
                    "init: phase Idle");
    runner.expectEq(0u, p.totalBytes, "init: totalBytes is 0");
    runner.expectEq(0u, p.bytesFlashed, "init: bytesFlashed is 0");
  }

  // ============================================================
  // Firmware without SHA trailer (hashAppended=0) passes validation
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data = buildMinSizeFirmware(false);
    SdMan.setFileData(PAPYRIX_FIRMWARE_FILE, data);
    FW_UPDATER.findFirmwareFile();
    bool ok = FW_UPDATER.beginUpdate();
    runner.expectTrue(ok, "no-sha firmware: beginUpdate succeeds");
    while (FW_UPDATER.pump()) {}
    runner.expectEq(static_cast<int>(FirmwareUpdatePhase::Complete),
                    static_cast<int>(FW_UPDATER.progress().phase), "no-sha firmware: Complete");
  }

  // ============================================================
  // findFirmwareFile replaces previous path
  // ============================================================

  {
    SdMan.reset();
    FW_UPDATER.reset();
    ESP_PARTITION.reset();
    auto data1 = buildMinSizeFirmware();
    SdMan.setFileData("/first.bin", data1);
    SdMan.setFileData("/second.bin", data1);

    FW_UPDATER.findFirmwareFile("/first.bin");
    runner.expectTrue(FW_UPDATER.isFirmwareAvailable(), "replace path: first found");

    FW_UPDATER.findFirmwareFile("/missing.bin");
    runner.expectFalse(FW_UPDATER.isFirmwareAvailable(), "replace path: missing not found");

    FW_UPDATER.findFirmwareFile("/second.bin");
    runner.expectTrue(FW_UPDATER.isFirmwareAvailable(), "replace path: second found");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
