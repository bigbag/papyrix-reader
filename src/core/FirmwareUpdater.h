#pragma once

#include <SdFat.h>
#include <esp_partition.h>

#include <cstdint>
#include <cstring>

#include "../config.h"

namespace papyrix {

enum class FirmwareUpdateError : uint8_t {
  None = 0,
  NoFile,
  FileOpenError,
  FileReadError,
  FlashError,
  Aborted,
  TooSmall,
  TooLarge,
  BadMagic,
  BadSegments,
  BadChecksum,
  BadSha,
  BadSize,
  NoPartition,
  OtadataFail,
};

enum class FirmwareUpdatePhase : uint8_t {
  Idle,
  Validating,
  Flashing,
  Complete,
  Error,
};

struct FirmwareUpdateProgress {
  FirmwareUpdatePhase phase = FirmwareUpdatePhase::Idle;
  FirmwareUpdateError error = FirmwareUpdateError::None;
  uint32_t bytesFlashed = 0;
  uint32_t totalBytes = 0;
};

class FirmwareUpdater {
 public:
  static FirmwareUpdater& instance();
  FirmwareUpdater(const FirmwareUpdater&) = delete;
  FirmwareUpdater& operator=(const FirmwareUpdater&) = delete;

  bool findFirmwareFile(const char* path);
  bool findFirmwareFile() { return findFirmwareFile(PAPYRIX_FIRMWARE_FILE); }
  bool isFirmwareAvailable() const { return available_; }

  bool beginUpdate();
  bool pump();
  const FirmwareUpdateProgress& progress() const { return progress_; }
  void abort();
  void reset();

 private:
  FirmwareUpdater() = default;

  FirmwareUpdateError validate();

  bool available_ = false;
  char path_[64] = "";
  FsFile file_;
  FirmwareUpdateProgress progress_;
  bool abortRequested_ = false;
  const esp_partition_t* destPartition_ = nullptr;
  size_t erasedUpto_ = 0;
};

#define FW_UPDATER FirmwareUpdater::instance()

}  // namespace papyrix
