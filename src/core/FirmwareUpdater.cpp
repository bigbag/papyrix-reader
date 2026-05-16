#include "FirmwareUpdater.h"

#include <SDCardManager.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <spi_flash_mmap.h>

#include <algorithm>
#include <cstring>

#include "OtaBootSwitch.h"

namespace papyrix {

namespace {
constexpr uint8_t ESP_IMAGE_MAGIC = 0xE9;
constexpr size_t MIN_FIRMWARE_SIZE = 64 * 1024;
constexpr size_t CHUNK = 4096;
constexpr size_t BLOCK = 64 * 1024;
constexpr size_t SHA_TRAILER = 32;
constexpr uint8_t CHECKSUM_SEED = 0xEF;
constexpr size_t HEADER_SIZE = 24;
constexpr size_t SEG_HEADER_SIZE = 8;
}  // namespace

FirmwareUpdater& FirmwareUpdater::instance() {
  static FirmwareUpdater mgr;
  return mgr;
}

bool FirmwareUpdater::findFirmwareFile(const char* path) {
  reset();
  strncpy(path_, path, sizeof(path_) - 1);
  path_[sizeof(path_) - 1] = '\0';
  available_ = SdMan.exists(path_);
  return available_;
}

FirmwareUpdateError FirmwareUpdater::validate() {
  FsFile f = SdMan.open(path_, O_RDONLY);
  if (!f) return FirmwareUpdateError::FileOpenError;

  const size_t fileSize = f.size();
  if (fileSize < MIN_FIRMWARE_SIZE) {
    f.close();
    return FirmwareUpdateError::TooSmall;
  }
  if (destPartition_ && fileSize > destPartition_->size) {
    f.close();
    return FirmwareUpdateError::TooLarge;
  }

  uint8_t header[HEADER_SIZE];
  if (f.read(header, HEADER_SIZE) != static_cast<int>(HEADER_SIZE)) {
    f.close();
    return FirmwareUpdateError::FileReadError;
  }
  if (header[0] != ESP_IMAGE_MAGIC) {
    f.close();
    return FirmwareUpdateError::BadMagic;
  }

  const uint8_t segCount = header[1];
  const bool hashAppended = header[23] != 0;

  uint8_t buf[CHUNK];
  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  mbedtls_sha256_starts(&shaCtx, 0);
  mbedtls_sha256_update(&shaCtx, header, HEADER_SIZE);

  uint8_t xorAccum = CHECKSUM_SEED;
  size_t pos = HEADER_SIZE;

  for (uint8_t i = 0; i < segCount; i++) {
    if (pos + SEG_HEADER_SIZE > fileSize) {
      mbedtls_sha256_free(&shaCtx);
      f.close();
      return FirmwareUpdateError::BadSegments;
    }
    uint8_t segHdr[SEG_HEADER_SIZE];
    if (f.read(segHdr, SEG_HEADER_SIZE) != static_cast<int>(SEG_HEADER_SIZE)) {
      mbedtls_sha256_free(&shaCtx);
      f.close();
      return FirmwareUpdateError::FileReadError;
    }
    mbedtls_sha256_update(&shaCtx, segHdr, SEG_HEADER_SIZE);
    pos += SEG_HEADER_SIZE;

    uint32_t dataLen;
    std::memcpy(&dataLen, segHdr + 4, sizeof(dataLen));
    if (pos + dataLen > fileSize) {
      mbedtls_sha256_free(&shaCtx);
      f.close();
      return FirmwareUpdateError::BadSegments;
    }

    size_t remaining = dataLen;
    while (remaining > 0) {
      const size_t want = std::min<size_t>(CHUNK, remaining);
      const int got = f.read(buf, want);
      if (got <= 0 || static_cast<size_t>(got) != want) {
        mbedtls_sha256_free(&shaCtx);
        f.close();
        return FirmwareUpdateError::FileReadError;
      }
      mbedtls_sha256_update(&shaCtx, buf, want);
      uint8_t acc = xorAccum;
      for (size_t j = 0; j < want; j++) acc ^= buf[j];
      xorAccum = acc;
      remaining -= want;
    }
    pos += dataLen;
  }

  const size_t padEnd = (pos + 16) & ~static_cast<size_t>(15);
  const size_t expectedTotal = padEnd + (hashAppended ? SHA_TRAILER : 0);
  if (expectedTotal != fileSize) {
    mbedtls_sha256_free(&shaCtx);
    f.close();
    return FirmwareUpdateError::BadSize;
  }

  const size_t padLen = padEnd - pos;
  uint8_t padBuf[16];
  if (padLen > sizeof(padBuf)) {
    mbedtls_sha256_free(&shaCtx);
    f.close();
    return FirmwareUpdateError::BadSize;
  }
  if (padLen > 0 && f.read(padBuf, padLen) != static_cast<int>(padLen)) {
    mbedtls_sha256_free(&shaCtx);
    f.close();
    return FirmwareUpdateError::FileReadError;
  }
  mbedtls_sha256_update(&shaCtx, padBuf, padLen);

  const uint8_t storedChecksum = padBuf[padLen - 1];
  if (xorAccum != storedChecksum) {
    mbedtls_sha256_free(&shaCtx);
    f.close();
    return FirmwareUpdateError::BadChecksum;
  }

  if (hashAppended) {
    uint8_t computed[SHA_TRAILER];
    mbedtls_sha256_finish(&shaCtx, computed);
    uint8_t stored[SHA_TRAILER];
    if (f.read(stored, SHA_TRAILER) != static_cast<int>(SHA_TRAILER)) {
      mbedtls_sha256_free(&shaCtx);
      f.close();
      return FirmwareUpdateError::FileReadError;
    }
    if (std::memcmp(computed, stored, SHA_TRAILER) != 0) {
      mbedtls_sha256_free(&shaCtx);
      f.close();
      return FirmwareUpdateError::BadSha;
    }
  }

  mbedtls_sha256_free(&shaCtx);
  f.close();
  return FirmwareUpdateError::None;
}

bool FirmwareUpdater::beginUpdate() {
  if (!available_) {
    progress_.phase = FirmwareUpdatePhase::Error;
    progress_.error = FirmwareUpdateError::NoFile;
    return false;
  }

  destPartition_ = esp_ota_get_next_update_partition(nullptr);
  if (!destPartition_) {
    progress_.phase = FirmwareUpdatePhase::Error;
    progress_.error = FirmwareUpdateError::NoPartition;
    return false;
  }

  progress_.phase = FirmwareUpdatePhase::Validating;
  FirmwareUpdateError err = validate();
  if (err != FirmwareUpdateError::None) {
    progress_.phase = FirmwareUpdatePhase::Error;
    progress_.error = err;
    return false;
  }

  file_ = SdMan.open(path_, O_RDONLY);
  if (!file_) {
    progress_.phase = FirmwareUpdatePhase::Error;
    progress_.error = FirmwareUpdateError::FileOpenError;
    return false;
  }

  progress_.totalBytes = file_.size();
  progress_.bytesFlashed = 0;
  erasedUpto_ = 0;
  progress_.phase = FirmwareUpdatePhase::Flashing;
  return true;
}

bool FirmwareUpdater::pump() {
  if (progress_.phase != FirmwareUpdatePhase::Flashing) return false;

  if (abortRequested_) {
    file_.close();
    progress_.phase = FirmwareUpdatePhase::Error;
    progress_.error = FirmwareUpdateError::Aborted;
    return false;
  }

  const size_t streamPos = static_cast<size_t>(progress_.bytesFlashed);
  const size_t firmwareSize = static_cast<size_t>(progress_.totalBytes);

  if (streamPos >= firmwareSize) {
    file_.close();
    if (!ota_boot::switchTo(destPartition_)) {
      progress_.phase = FirmwareUpdatePhase::Error;
      progress_.error = FirmwareUpdateError::OtadataFail;
      return false;
    }
    progress_.phase = FirmwareUpdatePhase::Complete;
    return false;
  }

  if (streamPos >= erasedUpto_) {
    size_t eraseLen = std::min<size_t>(BLOCK, destPartition_->size - streamPos);
    eraseLen = (eraseLen + SPI_FLASH_SEC_SIZE - 1) & ~(SPI_FLASH_SEC_SIZE - 1);
    eraseLen = std::min<size_t>(eraseLen, destPartition_->size - streamPos);
    if (esp_partition_erase_range(destPartition_, streamPos, eraseLen) != ESP_OK) {
      file_.close();
      progress_.phase = FirmwareUpdatePhase::Error;
      progress_.error = FirmwareUpdateError::FlashError;
      return false;
    }
    erasedUpto_ = streamPos + eraseLen;
  }

  const size_t want = std::min<size_t>(CHUNK, firmwareSize - streamPos);
  uint8_t buf[CHUNK];
  const int got = file_.read(buf, want);
  if (got <= 0 || static_cast<size_t>(got) != want) {
    file_.close();
    progress_.phase = FirmwareUpdatePhase::Error;
    progress_.error = FirmwareUpdateError::FileReadError;
    return false;
  }

  if (esp_partition_write(destPartition_, streamPos, buf, want) != ESP_OK) {
    file_.close();
    progress_.phase = FirmwareUpdatePhase::Error;
    progress_.error = FirmwareUpdateError::FlashError;
    return false;
  }

  progress_.bytesFlashed += want;
  return true;
}

void FirmwareUpdater::abort() { abortRequested_ = true; }

void FirmwareUpdater::reset() {
  abortRequested_ = false;
  if (file_) file_.close();
  available_ = false;
  path_[0] = '\0';
  progress_ = FirmwareUpdateProgress{};
  destPartition_ = nullptr;
  erasedUpto_ = 0;
}

}  // namespace papyrix
