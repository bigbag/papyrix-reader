#pragma once

#include <driver/gpio.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_FAIL = -1;
inline constexpr int I2C_NUM_1 = 1;
inline constexpr int I2C_CLK_SRC_DEFAULT = 0;
inline constexpr int I2C_ADDR_BIT_LEN_7 = 0;

struct i2c_master_bus_config_t {
  int i2c_port = 0;
  gpio_num_t sda_io_num = -1;
  gpio_num_t scl_io_num = -1;
  int clk_source = 0;
  uint8_t glitch_ignore_cnt = 0;
  struct { unsigned enable_internal_pullup = 0; } flags;
};
struct i2c_device_config_t {
  int dev_addr_length = 0;
  uint16_t device_address = 0;
  uint32_t scl_speed_hz = 0;
};
struct MockNativeI2cBus { bool active = false; };
struct MockNativeI2cDevice { bool active = false; i2c_device_config_t config{}; };
using i2c_master_bus_handle_t = MockNativeI2cBus*;
using i2c_master_dev_handle_t = MockNativeI2cDevice*;

namespace test_native_i2c {
inline MockNativeI2cBus bus;
inline std::array<MockNativeI2cDevice, 2> devices;
inline i2c_master_bus_config_t config;
inline std::array<std::array<uint8_t, 256>, 128> registers{};
struct Transfer { uint16_t address; uint32_t hz; uint8_t reg; size_t length; bool write; int timeout; };
inline std::vector<Transfer> transfers;
inline unsigned operations = 0, failAt = 0, busCreations = 0, deviceCreations = 0;
inline size_t shortRead = 0;
inline std::vector<uint16_t> removals;
inline std::mutex transferMutex;
inline void (*readHook)(uint16_t, uint8_t, uint8_t*, size_t) = nullptr;
inline bool fail() { return ++operations == failAt; }
inline void failNext(unsigned offset = 1) { failAt = operations + offset; }
}

inline esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* config, i2c_master_bus_handle_t* out) {
  using namespace test_native_i2c;
  if (fail() || bus.active) return ESP_FAIL;
  test_native_i2c::config = *config;
  bus.active = true;
  ++busCreations;
  *out = &bus;
  return ESP_OK;
}
inline esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t* config,
                                         i2c_master_dev_handle_t* out) {
  using namespace test_native_i2c;
  if (fail() || !bus || !bus->active) return ESP_FAIL;
  for (auto& device : devices) {
    if (!device.active) {
      device = {true, *config};
      *out = &device;
      ++deviceCreations;
      return ESP_OK;
    }
  }
  return ESP_FAIL;
}
inline esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device) {
  if (!device || !device->active) return ESP_FAIL;
  test_native_i2c::removals.push_back(device->config.device_address);
  device->active = false;
  return ESP_OK;
}
inline esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus) {
  if (!bus || !bus->active) return ESP_FAIL;
  for (const auto& device : test_native_i2c::devices) if (device.active) return ESP_FAIL;
  bus->active = false;
  return ESP_OK;
}
inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t* data, size_t length, int timeout) {
  using namespace test_native_i2c;
  const std::lock_guard<std::mutex> lock(transferMutex);
  if (fail() || !device || !device->active || !data || length < 1) return ESP_FAIL;
  transfers.push_back({device->config.device_address, device->config.scl_speed_hz, data[0], length - 1, true, timeout});
  for (size_t i = 1; i < length; ++i) registers[device->config.device_address][uint8_t(data[0] + i - 1)] = data[i];
  return ESP_OK;
}
inline esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t* reg, size_t regLength,
                                          uint8_t* data, size_t length, int timeout) {
  using namespace test_native_i2c;
  const std::lock_guard<std::mutex> lock(transferMutex);
  if (fail() || !device || !device->active || !data || !reg || regLength != 1) return ESP_FAIL;
  transfers.push_back({device->config.device_address, device->config.scl_speed_hz, *reg, length, false, timeout});
  const size_t received = shortRead && shortRead < length ? shortRead : length;
  for (size_t i = 0; i < received; ++i) data[i] = registers[device->config.device_address][uint8_t(*reg + i)];
  if (received < length) shortRead = 0;
  if (readHook) readHook(device->config.device_address, *reg, data, length);
  return received < length ? ESP_FAIL : ESP_OK;
}
