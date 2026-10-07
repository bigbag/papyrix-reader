#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

class TwoWire {
 public:
  TwoWire() { std::memset(present_, 1, sizeof(present_)); }

  bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0) {
    ++beginCount_;
    sda_ = sda;
    scl_ = scl;
    frequency_ = frequency;
    return true;
  }
  void end() { invalidState_ = false; }
  void setTimeOut(uint16_t timeout) { timeout_ = timeout; }

  void beginTransmission(uint8_t address) {
    txAddress_ = address;
    txLength_ = 0;
  }

  size_t write(uint8_t value) {
    if (txLength_ < sizeof(tx_)) tx_[txLength_] = value;
    ++txLength_;
    return 1;
  }

  uint8_t endTransmission(bool stop = true) {
    if (writeHook_ && txLength_ > 0 && !(!stop && txLength_ == 1)) {
      registerPointer_[txAddress_] = tx_[0];
      return writeHook_(txAddress_, tx_, txLength_) ? 0 : 2;
    }
    if (!present_[txAddress_]) return 4;
    if (txLength_ == 0) return 0;
    const size_t width = wideAddress_[txAddress_] ? 2 : 1;
    if (txLength_ < width || (failWrites_ && txLength_ > width)) return 4;
    const uint16_t reg = width == 2 ? static_cast<uint16_t>((tx_[0] << 8) | tx_[1]) : tx_[0];
    registerPointer_[txAddress_] = reg;
    for (size_t i = width; i < txLength_ && i < sizeof(tx_); ++i) {
      const uint16_t target = static_cast<uint16_t>(reg + i - width);
      setRegister(txAddress_, target, tx_[i]);
      ++registerWrites_[(static_cast<uint32_t>(txAddress_) << 16) | target];
    }
    if (width == 1 && txLength_ == 3 && write16Count_ < kWrite16Capacity) {
      write16_[write16Count_++] = {txAddress_, tx_[0],
                                   static_cast<uint16_t>(tx_[1] | (static_cast<uint16_t>(tx_[2]) << 8))};
    }
    return 0;
  }

  uint8_t requestFrom(int address, int length) {
    if (readHook_) {
      rxIndex_ = 0;
      const size_t n = static_cast<size_t>(length) > sizeof(rx_) ? sizeof(rx_) : static_cast<size_t>(length);
      rxLength_ = readHook_(static_cast<uint8_t>(address), static_cast<uint8_t>(registerPointer_[address]), rx_, n) ? n : 0;
      return static_cast<uint8_t>(rxLength_);
    }
    if (invalidState_) return 0;
    if (faultRemaining_ > 0 && faultAddress_ == address && registerPointer_[address] == faultReg_) {
      --faultRemaining_;
      return 0;
    }
    rxLength_ = static_cast<size_t>(length) > sizeof(rx_) ? sizeof(rx_) : static_cast<size_t>(length);
    if (rxLength_ > readLimit_) rxLength_ = readLimit_;
    rxIndex_ = 0;
    const uint16_t reg = registerPointer_[address];
    for (size_t i = 0; i < rxLength_; ++i) rx_[i] = getRegister(address, static_cast<uint16_t>(reg + i));
    registerPointer_[address] = static_cast<uint16_t>(reg + rxLength_);
    return static_cast<uint8_t>(rxLength_);
  }

  uint8_t requestFrom(int address, int length, uint8_t) { return requestFrom(address, length); }

  int available() { return static_cast<int>(rxLength_ - rxIndex_); }
  int read() { return rxIndex_ < rxLength_ ? rx_[rxIndex_++] : -1; }

  void reset() {
    registerWrites_.clear();
    registers_.clear();
    std::memset(wideAddress_, 0, sizeof(wideAddress_));
    failWrites_ = false;
    readLimit_ = sizeof(rx_);
    invalidState_ = false;
    faultAddress_ = 0;
    faultReg_ = 0;
    faultRemaining_ = 0;
    std::memset(present_, 0, sizeof(present_));
    std::memset(registerPointer_, 0, sizeof(registerPointer_));
    txLength_ = 0;
    rxLength_ = 0;
    rxIndex_ = 0;
    write16Count_ = 0;
    sda_ = -1;
    scl_ = -1;
    frequency_ = 0;
    timeout_ = 0;
    beginCount_ = 0;
    writeHook_ = nullptr;
    readHook_ = nullptr;
  }

  void setPresent(uint8_t address, bool present) { present_[address] = present; }
  void setInvalidState(bool invalid) { invalidState_ = invalid; }
  void setWideAddress(uint8_t address) { wideAddress_[address] = true; }
  void setWriteFailure(bool fail) { failWrites_ = fail; }
  void setReadLimit(size_t limit) { readLimit_ = limit; }
  // NACK the next `occurrences` reads of one register, then let them succeed.
  void setTransientReadFault(uint8_t address, uint16_t reg, uint16_t occurrences) {
    faultAddress_ = address;
    faultReg_ = reg;
    faultRemaining_ = occurrences;
  }
  // Optional device model. When set it replaces the register-file behavior, so
  // a test can model a real chip's responses and quirks.
  void setDeviceHooks(bool (*writeHook)(uint8_t, const uint8_t*, size_t),
                      bool (*readHook)(uint8_t, uint8_t, uint8_t*, size_t)) {
    writeHook_ = writeHook;
    readHook_ = readHook;
  }
  void setRegister(uint8_t address, uint16_t reg, uint8_t value) {
    registers_[(static_cast<uint32_t>(address) << 16) | reg] = value;
  }
  uint8_t getRegister(uint8_t address, uint16_t reg) const {
    const auto it = registers_.find((static_cast<uint32_t>(address) << 16) | reg);
    return it == registers_.end() ? 0 : it->second;
  }
  void setRegister16(uint8_t address, uint8_t reg, uint16_t value) {
    setRegister(address, reg, static_cast<uint8_t>(value));
    setRegister(address, reg + 1, static_cast<uint8_t>(value >> 8));
  }
  uint16_t getRegister16(uint8_t address, uint8_t reg) const {
    return static_cast<uint16_t>(getRegister(address, reg)) |
           (static_cast<uint16_t>(getRegister(address, reg + 1)) << 8);
  }
  bool sawWrite16(uint8_t address, uint8_t reg, uint16_t value) const {
    for (size_t i = 0; i < write16Count_; ++i) {
      if (write16_[i].address == address && write16_[i].reg == reg && write16_[i].value == value) return true;
    }
    return false;
  }

  int sdaPin() const { return sda_; }
  int sclPin() const { return scl_; }
  uint32_t frequency() const { return frequency_; }
  size_t beginCount() const { return beginCount_; }
  size_t registerWriteCount(uint8_t address, uint16_t reg) const {
    const auto it = registerWrites_.find((static_cast<uint32_t>(address) << 16) | reg);
    return it == registerWrites_.end() ? 0 : it->second;
  }

 private:
  struct Write16Event {
    uint8_t address;
    uint8_t reg;
    uint16_t value;
  };

  static constexpr size_t kWrite16Capacity = 128;
  bool present_[128]{};
  std::unordered_map<uint32_t, uint8_t> registers_;
  uint16_t registerPointer_[128]{};
  bool wideAddress_[128]{};
  bool failWrites_ = false;
  size_t readLimit_ = 32;
  uint8_t txAddress_ = 0;
  uint8_t tx_[32]{};
  size_t txLength_ = 0;
  uint8_t rx_[32]{};
  size_t rxLength_ = 0;
  size_t rxIndex_ = 0;
  Write16Event write16_[kWrite16Capacity]{};
  size_t write16Count_ = 0;
  size_t beginCount_ = 0;
  int sda_ = -1;
  int scl_ = -1;
  uint32_t frequency_ = 0;
  uint16_t timeout_ = 0;
  bool invalidState_ = false;
  uint8_t faultAddress_ = 0;
  uint16_t faultReg_ = 0;
  uint16_t faultRemaining_ = 0;
  std::unordered_map<uint32_t, size_t> registerWrites_;
  bool (*writeHook_)(uint8_t, const uint8_t*, size_t) = nullptr;
  bool (*readHook_)(uint8_t, uint8_t, uint8_t*, size_t) = nullptr;
};

extern TwoWire Wire;
