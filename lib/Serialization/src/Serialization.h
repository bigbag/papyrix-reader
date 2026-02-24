#pragma once
#include <Logging.h>
#include <SdFat.h>

#include <iostream>

namespace serialization {
template <typename T>
static void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
static void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
static void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
static void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

template <typename T>
[[nodiscard]] static bool readPodChecked(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

static void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

static void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

[[nodiscard]] static bool readString(std::istream& is, std::string& s) {
  uint32_t len;
  readPod(is, len);
  if (!is.good()) {
    s.clear();
    return false;
  }
  if (len > 65536) {  // Sanity check: no string should be > 64KB
    s.clear();
    is.setstate(std::ios::failbit);
    return false;
  }
  s.resize(len);
  is.read(&s[0], len);
  return is.good();
}

[[nodiscard]] static bool readString(FsFile& file, std::string& s) {
  uint32_t len;
  if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) {
    s.clear();
    return false;
  }
  if (len > 65536) {  // Sanity check: no string should be > 64KB
    LOG_ERR("SERIAL", "String length %u exceeds max, file corrupt", len);
    s.clear();
    return false;
  }
  s.resize(len);
  if (len > 0 && file.read(reinterpret_cast<uint8_t*>(&s[0]), len) != static_cast<int>(len)) {
    s.clear();
    return false;
  }
  return true;
}

template <typename T>
static void readPodValidated(FsFile& file, T& value, T maxValue) {
  T temp;
  file.read(reinterpret_cast<uint8_t*>(&temp), sizeof(T));
  if (temp < maxValue) {
    value = temp;
  }
}
}  // namespace serialization
