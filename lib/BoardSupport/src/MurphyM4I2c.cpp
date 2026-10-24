#include "MurphyM4I2c.h"

#include <TargetConfig.h>

#if PAPYRIX_TARGET_MURPHY_M4
#include <driver/i2c_master.h>

namespace papyrix::board::murphy_m4_i2c {
namespace {
i2c_master_bus_handle_t bus = nullptr;
i2c_master_dev_handle_t touch = nullptr;
i2c_master_dev_handle_t rtc = nullptr;

i2c_master_dev_handle_t handle(Device device) {
  return device == Device::Touch ? touch : device == Device::Rtc ? rtc : nullptr;
}
}  // namespace

bool begin() {
  if (bus && touch && rtc) return true;
  i2c_master_bus_config_t config{};
  config.i2c_port = I2C_NUM_1;
  config.sda_io_num = static_cast<gpio_num_t>(13);
  config.scl_io_num = static_cast<gpio_num_t>(12);
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = 1;
  if (i2c_new_master_bus(&config, &bus) != ESP_OK) {
    bus = nullptr;
    return false;
  }
  i2c_device_config_t device{};
  device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device.device_address = 0x2E;
  device.scl_speed_hz = 100000;
  if (i2c_master_bus_add_device(bus, &device, &touch) == ESP_OK) {
    device.device_address = 0x32;
    device.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &device, &rtc) == ESP_OK) return true;
    i2c_master_bus_rm_device(touch);
    touch = nullptr;
  }
  rtc = nullptr;
  i2c_del_master_bus(bus);
  bus = nullptr;
  return false;
}

bool read(Device device, uint8_t reg, uint8_t* data, size_t length) {
  const auto dev = handle(device);
  return dev && data && length && i2c_master_transmit_receive(dev, &reg, 1, data, length, 20) == ESP_OK;
}

bool write(Device device, const uint8_t* data, size_t length) {
  const auto dev = handle(device);
  return dev && data && length && i2c_master_transmit(dev, data, length, 20) == ESP_OK;
}
}  // namespace papyrix::board::murphy_m4_i2c
#endif
