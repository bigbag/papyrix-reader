#!/usr/bin/env python3
"""Run the SDMMC lifecycle against a recorded host and power rail."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

MOCK = r'''
#pragma once
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
using esp_err_t = int;
using gpio_num_t = int;
using Sector_t = uint32_t;
constexpr int ESP_OK=0, GPIO_NUM_NC=-1, SDMMC_FREQ_DEFAULT=20000;
constexpr int SDMMC_SLOT_FLAG_INTERNAL_PULLUP=1, MALLOC_CAP_DMA=1, MALLOC_CAP_8BIT=2;
constexpr int HIGH=1, LOW=0, OUTPUT=1, GPIO_FLOATING=0;
inline bool hostUp=false, railOn=false, failAllocation=false, failHost=false, failSlot=false, failIo=false;
inline bool driven[64]{}, pulled[64]{};
inline unsigned elapsed=0, poweredAt=0;
inline int allocations=0, hostStarts=0, failures=0;
inline size_t largestBlock=65536, allocationCalls=0;
inline std::array<int,6> signals{-1,-1,-1,-1,-1,-1};
inline std::array<uint8_t,128*512> disk{};
inline void validPin(int pin) {assert(pin>=0 && pin<64);}
inline void gpio_hold_dis(int pin) {validPin(pin);}
inline void gpio_reset_pin(int pin) {validPin(pin);driven[pin]=false;pulled[pin]=true;}
inline void gpio_set_pull_mode(int pin,int mode) {validPin(pin);assert(mode==GPIO_FLOATING);pulled[pin]=false;}
inline void digitalWrite(int pin,int value) {
  validPin(pin);
  if(pin==SD_POWER_PIN) {
    if(value==HIGH) {
      assert(!hostUp);
      for(int signal:signals) if(signal>=0) assert(!driven[signal] && !pulled[signal]);
    }
    railOn=value==LOW;
    if(railOn) poweredAt=elapsed;
  }
}
inline void pinMode(int pin,int) {validPin(pin);}
inline void delay(unsigned ms) {elapsed+=ms;}
inline size_t heap_caps_get_largest_free_block(int caps) {
  assert(caps==(MALLOC_CAP_DMA|MALLOC_CAP_8BIT));return largestBlock;
}
inline void* heap_caps_malloc(size_t bytes,int) {
  ++allocationCalls;
  if(failAllocation) return nullptr;
  ++allocations;return std::malloc(bytes);
}
inline void heap_caps_free(void* p) {if(p) --allocations;std::free(p);}
struct sdmmc_host_t {int max_freq_khz=0,slot=1;};
struct sdmmc_slot_config_t {int width=0,clk=0,cmd=0,d0=0,d1=0,d2=0,d3=0,flags=0;};
struct sdmmc_card_t {struct {uint32_t capacity=0;} csd;};
inline sdmmc_slot_config_t lastSlot{};
#define SDMMC_HOST_DEFAULT() sdmmc_host_t{}
#define SDMMC_SLOT_CONFIG_DEFAULT() sdmmc_slot_config_t{}
inline int sdmmc_host_init() {
  assert(railOn && elapsed-poweredAt>=120);++hostStarts;
  if(failHost) return -1;hostUp=true;return 0;
}
inline int sdmmc_host_init_slot(int,const sdmmc_slot_config_t* slot) {
  assert(hostUp);lastSlot=*slot;
  signals={slot->clk,slot->cmd,slot->d0,slot->d1,slot->d2,slot->d3};
  for(int pin:signals) if(pin>=0) {validPin(pin);driven[pin]=pulled[pin]=true;}
  return failSlot?-1:0;
}
inline int sdmmc_host_deinit() {hostUp=false;return 0;}
inline int sdmmc_card_init(const sdmmc_host_t*,sdmmc_card_t* c) {
  assert(hostUp && railOn);if(failures>0){--failures;return -1;}c->csd.capacity=128;return 0;
}
inline int sdmmc_read_sectors(sdmmc_card_t*,void* p,Sector_t sector,size_t n) {
  assert(hostUp && railOn);assert(n<=8);
  if(failIo || sector>=128 || n>128-sector) return -1;
  std::memcpy(p,disk.data()+sector*512,n*512);return 0;
}
inline int sdmmc_write_sectors(sdmmc_card_t*,const void* p,Sector_t sector,size_t n) {
  assert(hostUp && railOn);assert(n<=8);
  if(failIo || sector>=128 || n>128-sector) return -1;
  std::memcpy(disk.data()+sector*512,p,n*512);return 0;
}
class FsBlockDeviceInterface {
public:
virtual ~FsBlockDeviceInterface()=default;
virtual void end()=0;
virtual bool isBusy()=0;
virtual bool readSector(Sector_t,uint8_t*)=0;
virtual bool readSectors(Sector_t,uint8_t*,size_t)=0;
virtual bool writeSector(Sector_t,const uint8_t*)=0;
virtual bool writeSectors(Sector_t,const uint8_t*,size_t)=0;
virtual Sector_t sectorCount()=0;
virtual bool syncDevice()=0;
};
'''

MAIN = r'''
#include "SdmmcBlockDevice.h"
#include "MockSd.h"
#include <BoardProfiles.h>
int main() {
  const auto& config=papyrix::board::bootProfile().storage;
  papyrix::sd::SdmmcBlockDevice card;
  failures=2;
  assert(card.begin(config));
  assert(hostStarts==3 && allocations==1);
  assert(card.begin(config));
  assert(hostStarts==3 && allocations==1);
#if PAPYRIX_TARGET_MURPHY_M4
  assert(lastSlot.width==4 && lastSlot.clk==16 && lastSlot.cmd==15 && lastSlot.d0==17 &&
         lastSlot.d1==18 && lastSlot.d2==11 && lastSlot.d3==14);
#else
  assert(lastSlot.width==1 && lastSlot.d1==-1 && lastSlot.d2==-1 && lastSlot.d3==-1);
#endif
  std::array<uint8_t,9*512> written{},readback{};
  for(size_t i=0;i<written.size();++i) written[i]=uint8_t(i*37+i/512);
  assert(card.writeSectors(7,written.data(),9));
  assert(card.readSectors(7,readback.data(),9) && readback==written);
  failIo=true;
  assert(!card.readSectors(7,readback.data(),9) && !card.writeSectors(7,written.data(),9));
  failIo=false;
  card.end();assert(!hostUp && !railOn && allocations==0);
  card.end();assert(allocations==0);
  for(int failure=0;failure<6;++failure) {
    failures=failure==0?4:0;
    failAllocation=failure==1;
    failHost=failure==2;
    failSlot=failure==3;
    failIo=failure==4;
    largestBlock=failure==5?4095:65536;
    const auto before=allocationCalls;
    assert(!card.begin(config));
    assert(!hostUp && !railOn && allocations==0);
    if(failure==5) assert(allocationCalls==before);
    failAllocation=failHost=failSlot=failIo=false;largestBlock=65536;
    assert(card.begin(config));
    card.end();assert(!hostUp && !railOn && allocations==0);
  }
  auto invalid=config;invalid.sdmmcDat0=-1;
  assert(!card.begin(invalid) && !hostUp && !railOn);
#if PAPYRIX_TARGET_MURPHY_M4
  invalid=config;invalid.sdmmcDat2=-1;
  assert(!card.begin(invalid) && !hostUp && !railOn);
#endif
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="papyrix-sdmmc-") as directory:
        path = Path(directory)
        (path / "MockSd.h").write_text(MOCK)
        (path / "main.cpp").write_text(MAIN)
        for header in ("Arduino.h", "SdFat.h", "sdmmc_cmd.h", "esp_heap_caps.h",
                       "driver/gpio.h", "driver/sdmmc_host.h"):
            target = path / header
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('#pragma once\n#include "MockSd.h"\n')
        includes = [str(path), "lib/BoardSupport/include", "lib/EInkDisplay/include",
                    "lib/SDCardManager/include", "lib/SDCardManager/src", "lib/BatteryMonitor/include"]
        for target, power_pin in (("MURPHY_M4", 10), ("X4PRO", 5), ("X4CLASSIC", 6)):
            command = [os.environ.get("CXX", "c++"), "-std=c++17",
                       f"-DPAPYRIX_TARGET_{target}=1", f"-DSD_POWER_PIN={power_pin}"]
            command += ["-I" + include for include in includes]
            command += ["lib/SDCardManager/src/SdmmcBlockDevice.cpp",
                        "lib/BoardSupport/src/BoardProfiles.cpp", str(path / "main.cpp"),
                        "-o", str(path / "smoke")]
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([str(path / "smoke")], check=True)
    print("PASS: one/four-bit SDMMC round trips, retries, cleanup, remount, and DMA allocation failures")


if __name__ == "__main__":
    main()
