#!/usr/bin/env python3
"""Check SDMMC volume mounts with the repository SdFat sources."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <FsLib/FsVolume.h>
#include <array>
#include <cassert>
#include <cstring>
#define PAPYRIX_CAP_SDMMC 1
#define LOG_INF(...)
#define LOG_ERR(...)
namespace papyrix::board {
struct HardwareIdentity {
  struct Profile { int storage = 0; } value;
  static HardwareIdentity& instance() { static HardwareIdentity identity; return identity; }
  const Profile& profile() const { return value; }
};
}
class Disk : public FsBlockDeviceInterface {
 public:
  std::array<uint8_t, 512> boot{};
  bool partitioned = false;
  bool formatted = true;
  Disk() {
    boot[0] = 0xEB; boot[1] = 0x3C; boot[2] = 0x90;
    std::memcpy(boot.data() + 3, "MSDOS5.0", 8);
    boot[11] = 0; boot[12] = 2; boot[13] = 2;
    boot[14] = 1; boot[16] = 2;
    boot[17] = 0; boot[18] = 2;
    boot[19] = 0; boot[20] = 64;
    boot[21] = 0xF8; boot[22] = 32;
    boot[24] = 32; boot[26] = 64;
    boot[36] = 0x80; boot[38] = 0x29;
    std::memcpy(boot.data() + 43, "TEST       ", 11);
    std::memcpy(boot.data() + 54, "FAT16   ", 8);
    boot[510] = 0x55; boot[511] = 0xAA;
  }
  bool begin(int) { return true; }
  bool isBusy() override { return false; }
  bool syncDevice() override { return true; }
  Sector_t sectorCount() override { return 16385; }
  bool readSector(Sector_t sector, uint8_t* dst) override {
    if (sector >= sectorCount()) return false;
    std::memset(dst, 0, 512);
    if (!formatted) return true;
    const Sector_t start = partitioned ? 1 : 0;
    if (sector == start) {
      std::memcpy(dst, boot.data(), 512);
      dst[28] = start;
    }
    if (sector == start + 1 || sector == start + 33) {
      dst[0] = 0xF8;
      dst[1] = dst[2] = dst[3] = 0xFF;
    }
    if (partitioned && sector == 0) {
      dst[446 + 4] = 0x06; dst[446 + 8] = 1;
      dst[446 + 13] = 64;
      dst[510] = 0x55; dst[511] = 0xAA;
    }
    return true;
  }
  bool readSectors(Sector_t sector, uint8_t* dst, size_t n) override {
    for (size_t i = 0; i < n; ++i) {
      if (!readSector(sector + i, dst + 512 * i)) return false;
    }
    return true;
  }
  bool writeSector(Sector_t, const uint8_t*) override { return false; }
  bool writeSectors(Sector_t, const uint8_t*, size_t) override { return false; }
};
Disk sdmmc;
class SDCardManager {
 public:
  FsVolume sd;
  bool initialized = false;
  bool begin();
  void end();
};
@MOUNT@
int main() {
  SDCardManager manager;
  assert(manager.begin());
  manager.end();
  sdmmc.partitioned = true;
  assert(manager.begin());
  manager.end();
  sdmmc.formatted = false;
  assert(!manager.begin());
}
'''


def main():
    sdfat = ROOT / "lib/SdFat/src"
    sources = {
        "FatLib": "FatFile FatFileLFN FatFilePrint FatFileSFN FatName FatPartition FatVolume",
        "ExFatLib": "ExFatFile ExFatFilePrint ExFatFileWrite ExFatName ExFatPartition ExFatVolume",
        "FsLib": "FsFile FsNew FsVolume",
        "common": "FmtNumber FsCache FsDateTime FsName FsStructs FsUtf PrintBasic upcase",
    }
    source = (ROOT / "lib/SDCardManager/src/SDCardManager.cpp").read_text()
    start = source.index("bool SDCardManager::begin()")
    mount = source[start:source.index("bool SDCardManager::ready()", start)]
    with tempfile.TemporaryDirectory(prefix="papyrix-sdmmc-mount-") as directory:
        path = Path(directory)
        (path / "mount.cpp").write_text(HARNESS.replace("@MOUNT@", mount))
        (path / "host.h").write_text(
            "#pragma once\nclass __FlashStringHelper;\ninline unsigned long millis() { return 0; }\n")
        command = [
            os.environ.get("CXX", "c++"), "-std=c++17", "-ffunction-sections", "-fdata-sections",
            "-DENABLE_ARDUINO_FEATURES=0", "-DENABLE_ARDUINO_SERIAL=0", "-DENABLE_ARDUINO_STRING=0",
            "-DUSE_BLOCK_DEVICE_INTERFACE=1", "-DSPI_DRIVER_SELECT=3",
            "-include", str(path / "host.h"), f"-I{sdfat}", str(path / "mount.cpp"),
        ]
        command += [str(sdfat / folder / (name + ".cpp"))
                    for folder, names in sources.items() for name in names.split()]
        command += ["-Wl,--gc-sections", "-o", str(path / "mount")]
        subprocess.run(command, check=True)
        subprocess.run([str(path / "mount")], check=True)
    print("PASS: SDMMC mounts whole-device and partitioned FAT16; rejects an invalid volume")


if __name__ == "__main__":
    main()
