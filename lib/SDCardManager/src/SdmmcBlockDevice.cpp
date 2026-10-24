#include "SdmmcBlockDevice.h"

#include <StoragePolicy.h>
#include <TargetConfig.h>

#if PAPYRIX_CAP_SDMMC

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>

#include <cstring>

namespace papyrix::sd {

bool SdmmcBlockDevice::begin(const board::StorageConfig& config) {
  const bool fourBit = config.transport == board::StorageTransport::Sdmmc4Bit;
  if (!fourBit && config.transport != board::StorageTransport::Sdmmc1Bit) return false;
  if (config.powerPin < 0 || config.sdmmcClk < 0 || config.sdmmcCmd < 0 || config.sdmmcDat0 < 0 ||
      (fourBit && (config.sdmmcDat1 < 0 || config.sdmmcDat2 < 0 || config.sdmmcDat3 < 0)))
    return false;
  if (hostReady_) return true;

  const auto plan = makeStorageMountPlan(config);
  if (!plan.controlsPowerPin) return false;
  config_ = config;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = fourBit ? 4 : 1;
  slot.clk = static_cast<gpio_num_t>(config.sdmmcClk);
  slot.cmd = static_cast<gpio_num_t>(config.sdmmcCmd);
  slot.d0 = static_cast<gpio_num_t>(config.sdmmcDat0);
  slot.d1 = fourBit ? static_cast<gpio_num_t>(config.sdmmcDat1) : GPIO_NUM_NC;
  slot.d2 = fourBit ? static_cast<gpio_num_t>(config.sdmmcDat2) : GPIO_NUM_NC;
  slot.d3 = fourBit ? static_cast<gpio_num_t>(config.sdmmcDat3) : GPIO_NUM_NC;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  gpio_hold_dis(static_cast<gpio_num_t>(config.powerPin));
  digitalWrite(config.powerPin, plan.activeLevel ? HIGH : LOW);
  pinMode(config.powerPin, OUTPUT);

  constexpr size_t transferBytes = kSectorSize * kMaxTransferSectors;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT) >= transferBytes)
    dmaBuffer_ = static_cast<uint8_t*>(heap_caps_malloc(transferBytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (!dmaBuffer_) {
    end();
    return false;
  }

  for (int attempt = 0; attempt < 4; ++attempt) {
    if (hostReady_) {
      sdmmc_host_deinit();
      hostReady_ = false;
    }
    releasePins();
    digitalWrite(config.powerPin, plan.inactiveLevel ? HIGH : LOW);
    delay(80);
    digitalWrite(config.powerPin, plan.activeLevel ? HIGH : LOW);
    delay(120);
    if (sdmmc_host_init() != ESP_OK) {
      end();
      return false;
    }
    hostReady_ = true;
    if (sdmmc_host_init_slot(host.slot, &slot) != ESP_OK) {
      end();
      return false;
    }
    card_ = {};
    const esp_err_t initResult = sdmmc_card_init(&host, &card_);
    if (initResult != ESP_OK && card_.csd.capacity == 0) continue;
    if (sdmmc_read_sectors(&card_, dmaBuffer_, 0, 1) == ESP_OK) return true;
  }

  end();
  return false;
}

void SdmmcBlockDevice::releasePins() const {
  for (const int pin : {config_.sdmmcClk, config_.sdmmcCmd, config_.sdmmcDat0, config_.sdmmcDat1, config_.sdmmcDat2,
                        config_.sdmmcDat3}) {
    if (pin == board::kPinUnused) continue;
    gpio_reset_pin(static_cast<gpio_num_t>(pin));
    gpio_set_pull_mode(static_cast<gpio_num_t>(pin), GPIO_FLOATING);
  }
}

void SdmmcBlockDevice::end() {
  if (dmaBuffer_) {
    heap_caps_free(dmaBuffer_);
    dmaBuffer_ = nullptr;
  }
  if (hostReady_) {
    sdmmc_host_deinit();
    hostReady_ = false;
  }
  if (config_.transport == board::StorageTransport::Sdmmc1Bit ||
      config_.transport == board::StorageTransport::Sdmmc4Bit) {
    releasePins();
    digitalWrite(config_.powerPin, config_.powerActiveHigh ? LOW : HIGH);
    config_ = {};
  }
  card_ = {};
}

bool SdmmcBlockDevice::readSectors(Sector_t sector, uint8_t* dst, size_t count) {
  if (!hostReady_ || !dmaBuffer_ || !dst || count == 0) return false;
  while (count > 0) {
    const size_t transferCount = count > kMaxTransferSectors ? kMaxTransferSectors : count;
    const size_t bytes = transferCount * kSectorSize;
    if (sdmmc_read_sectors(&card_, dmaBuffer_, sector, transferCount) != ESP_OK) return false;
    memcpy(dst, dmaBuffer_, bytes);
    sector += transferCount;
    dst += bytes;
    count -= transferCount;
  }
  return true;
}

bool SdmmcBlockDevice::writeSectors(Sector_t sector, const uint8_t* src, size_t count) {
  if (!hostReady_ || !dmaBuffer_ || !src || count == 0) return false;
  while (count > 0) {
    const size_t transferCount = count > kMaxTransferSectors ? kMaxTransferSectors : count;
    const size_t bytes = transferCount * kSectorSize;
    memcpy(dmaBuffer_, src, bytes);
    if (sdmmc_write_sectors(&card_, dmaBuffer_, sector, transferCount) != ESP_OK) return false;
    sector += transferCount;
    src += bytes;
    count -= transferCount;
  }
  return true;
}

Sector_t SdmmcBlockDevice::sectorCount() { return hostReady_ ? static_cast<Sector_t>(card_.csd.capacity) : 0; }

}  // namespace papyrix::sd

#endif
