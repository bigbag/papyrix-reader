#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#define ESP_OK 0
#define ESP_FAIL -1
#define SPI_FLASH_SEC_SIZE 4096

typedef int esp_err_t;

enum esp_partition_type_t {
  ESP_PARTITION_TYPE_APP = 0,
  ESP_PARTITION_TYPE_DATA = 1,
};

enum esp_partition_subtype_t {
  ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10,
  ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11,
  ESP_PARTITION_SUBTYPE_DATA_OTA = 0x00,
};

struct esp_partition_t {
  esp_partition_type_t type;
  esp_partition_subtype_t subtype;
  uint32_t address;
  uint32_t size;
  char label[17];
};

class EspPartitionMock {
 public:
  static EspPartitionMock& instance() {
    static EspPartitionMock inst;
    return inst;
  }

  void reset() {
    nextUpdatePartition_ = &ota1Partition_;
    otadataPartition_ = &otadataPartitionData_;
    eraseOk_ = true;
    writeOk_ = true;
    readOk_ = true;
    switchToOk_ = true;
    flashData_.clear();
    flashData_.resize(ota1Partition_.size, 0xFF);
    otadataData_.clear();
    otadataData_.resize(otadataPartitionData_.size, 0xFF);
    eraseCount_ = 0;
    writeCount_ = 0;
  }

  esp_partition_t ota0Partition_ = {ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, 0x10000, 0x640000, "app0"};
  esp_partition_t ota1Partition_ = {ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x650000, 0x640000, "app1"};
  esp_partition_t otadataPartitionData_ = {ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, 0xe000, 0x2000, "otadata"};

  const esp_partition_t* nextUpdatePartition_ = &ota1Partition_;
  const esp_partition_t* otadataPartition_ = &otadataPartitionData_;
  bool eraseOk_ = true;
  bool writeOk_ = true;
  bool readOk_ = true;
  bool switchToOk_ = true;
  std::vector<uint8_t> flashData_;
  std::vector<uint8_t> otadataData_;
  int eraseCount_ = 0;
  int writeCount_ = 0;
};

#define ESP_PARTITION EspPartitionMock::instance()

inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {
  return ESP_PARTITION.nextUpdatePartition_;
}

inline const esp_partition_t* esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype, const char*) {
  if (type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_DATA_OTA) {
    return ESP_PARTITION.otadataPartition_;
  }
  return nullptr;
}

inline esp_err_t esp_partition_erase_range(const esp_partition_t* part, size_t offset, size_t size) {
  if (!ESP_PARTITION.eraseOk_) return ESP_FAIL;
  ESP_PARTITION.eraseCount_++;
  if (part == ESP_PARTITION.nextUpdatePartition_) {
    if (offset + size <= ESP_PARTITION.flashData_.size()) {
      memset(ESP_PARTITION.flashData_.data() + offset, 0xFF, size);
    }
  } else if (part == ESP_PARTITION.otadataPartition_) {
    if (offset + size <= ESP_PARTITION.otadataData_.size()) {
      memset(ESP_PARTITION.otadataData_.data() + offset, 0xFF, size);
    }
  }
  return ESP_OK;
}

inline esp_err_t esp_partition_write(const esp_partition_t* part, size_t offset, const void* data, size_t size) {
  if (!ESP_PARTITION.writeOk_) return ESP_FAIL;
  ESP_PARTITION.writeCount_++;
  if (part == ESP_PARTITION.nextUpdatePartition_) {
    if (offset + size <= ESP_PARTITION.flashData_.size()) {
      memcpy(ESP_PARTITION.flashData_.data() + offset, data, size);
    }
  } else if (part == ESP_PARTITION.otadataPartition_) {
    if (offset + size <= ESP_PARTITION.otadataData_.size()) {
      memcpy(ESP_PARTITION.otadataData_.data() + offset, data, size);
    }
  }
  return ESP_OK;
}

inline esp_err_t esp_partition_read(const esp_partition_t* part, size_t offset, void* data, size_t size) {
  if (!ESP_PARTITION.readOk_) return ESP_FAIL;
  if (part == ESP_PARTITION.otadataPartition_) {
    if (offset + size <= ESP_PARTITION.otadataData_.size()) {
      memcpy(data, ESP_PARTITION.otadataData_.data() + offset, size);
    }
  }
  return ESP_OK;
}
