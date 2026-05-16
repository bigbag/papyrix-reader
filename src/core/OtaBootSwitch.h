#pragma once

#include <esp_partition.h>

#include <cstddef>
#include <cstdint>

namespace ota_boot {

struct __attribute__((packed)) SelectEntry {
  uint32_t ota_seq;
  uint8_t seq_label[20];
  uint32_t ota_state;
  uint32_t crc;
};
static_assert(sizeof(SelectEntry) == 32, "SelectEntry must be 32 bytes");

constexpr uint32_t kOtaImgNew = 0;
constexpr uint32_t kOtaImgInvalid = 3;
constexpr uint32_t kOtaImgAborted = 4;
constexpr size_t kOtaSeqCrcLen = 4;

uint32_t computeSeqCrc(uint32_t seq);
bool switchTo(const esp_partition_t* dest);

}  // namespace ota_boot
