#pragma once

#include <cstdint>
#include <map>
#include <string>

class Preferences {
 public:
  inline static bool failBegin = false;
  inline static bool failPut = false;
  inline static size_t putCalls = 0;
  bool begin(const char* name, bool = false) {
    if (failBegin) return false;
    namespace_ = name;
    return true;
  }
  void end() {}
  bool isKey(const char* key) const { return values_.count({namespace_, key}) != 0; }
  uint8_t getUChar(const char* key, uint8_t fallback = 0) const {
    const auto found = values_.find({namespace_, key});
    return found == values_.end() ? fallback : found->second;
  }
  size_t putUChar(const char* key, uint8_t value) {
    ++putCalls;
    if (failPut) return 0;
    values_[{namespace_, key}] = value;
    return 1;
  }
  bool remove(const char* key) { return values_.erase({namespace_, key}) != 0; }
 private:
  std::string namespace_;
  inline static std::map<std::pair<std::string, std::string>, uint8_t> values_;
};
