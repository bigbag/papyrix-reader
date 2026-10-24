#pragma once

#include <TargetConfig.h>

#if PAPYRIX_CAP_SDMMC

#include <BoardProfile.h>
#include <SdFat.h>
#include <sdmmc_cmd.h>

namespace papyrix::sd {

class SdmmcBlockDevice : public FsBlockDeviceInterface {
 public:
  bool begin(const board::StorageConfig& config);
  void end() override;

  bool isBusy() override { return false; }
  bool readSector(Sector_t sector, uint8_t* dst) override { return readSectors(sector, dst, 1); }
  bool readSectors(Sector_t sector, uint8_t* dst, size_t count) override;
  bool writeSector(Sector_t sector, const uint8_t* src) override { return writeSectors(sector, src, 1); }
  bool writeSectors(Sector_t sector, const uint8_t* src, size_t count) override;
  Sector_t sectorCount() override;
  bool syncDevice() override { return true; }

 private:
  void releasePins() const;

  static constexpr size_t kSectorSize = 512;
  static constexpr size_t kMaxTransferSectors = 8;

  sdmmc_card_t card_{};
  board::StorageConfig config_{};
  uint8_t* dmaBuffer_ = nullptr;
  bool hostReady_ = false;
};

}  // namespace papyrix::sd

#endif
