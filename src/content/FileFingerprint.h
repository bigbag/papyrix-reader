#pragma once

#include <Arduino.h>
#include <SDCardManager.h>
#include <SdFat.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <esp_random.h>
#endif

namespace papyrix {

namespace file_fingerprint_detail {
constexpr uint32_t kFnvBasis = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

inline uint32_t updateFnv1aBytes(uint32_t hash, const uint8_t* bytes, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= kFnvPrime;
  }
  return hash;
}

template <typename T>
inline uint32_t updateFnv1aValue(uint32_t hash, const T& value) {
  return updateFnv1aBytes(hash, reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}
}  // namespace file_fingerprint_detail

/**
 * Compute a cheap content fingerprint without scanning a potentially large book.
 * The file size, optional FAT modification time, and seven evenly-spaced
 * 128-byte samples are hashed. A zero result means the file could not be opened
 * or sampled completely after bounded retries.
 */
inline uint32_t fileFingerprint(const char* path) {
  if (!path || !*path) return 0;

  constexpr uint32_t kSampleSize = 128;
  constexpr uint32_t kSampleCount = 7;
  constexpr uint8_t kAttempts = 3;

  for (uint8_t attempt = 0; attempt < kAttempts; ++attempt) {
    FsFile file;
    if (!SdMan.openFileForRead("FP", path, file)) {
      if (attempt + 1 < kAttempts) delay(50);
      continue;
    }

    const uint32_t fileSize = file.size();
    uint32_t hash = file_fingerprint_detail::updateFnv1aValue(file_fingerprint_detail::kFnvBasis, fileSize);
    uint16_t modifyDate = 0;
    uint16_t modifyTime = 0;
    if (file.getModifyDateTime(&modifyDate, &modifyTime)) {
      hash = file_fingerprint_detail::updateFnv1aValue(hash, modifyDate);
      hash = file_fingerprint_detail::updateFnv1aValue(hash, modifyTime);
    }

    uint8_t sample[kSampleSize];
    const uint32_t span = fileSize > kSampleSize ? fileSize - kSampleSize : 0;
    uint32_t previousOffset = UINT32_MAX;
    bool sampled = true;

    for (uint32_t i = 0; i < kSampleCount; ++i) {
      const uint32_t offset =
          kSampleCount > 1 ? static_cast<uint32_t>(static_cast<uint64_t>(span) * i / (kSampleCount - 1)) : 0;
      if (offset == previousOffset) continue;
      previousOffset = offset;

      const uint32_t bytesToRead = std::min(kSampleSize, fileSize - offset);
      if (!file.seek(offset) || file.read(sample, bytesToRead) != bytesToRead) {
        sampled = false;
        break;
      }
      hash = file_fingerprint_detail::updateFnv1aValue(hash, offset);
      hash = file_fingerprint_detail::updateFnv1aBytes(hash, sample, bytesToRead);
    }

    file.close();
    if (sampled) return hash == 0 ? 1 : hash;
    if (attempt + 1 < kAttempts) delay(50);
  }

  return 0;
}

inline uint32_t sessionCacheFingerprint() {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  const uint32_t fingerprint = esp_random();
  return fingerprint == 0 ? 1 : fingerprint;
#else
  static std::atomic<uint32_t> next{0x80000000u};
  const uint32_t fingerprint = next.fetch_add(1, std::memory_order_relaxed);
  return fingerprint == 0 ? 1 : fingerprint;
#endif
}

inline uint32_t fingerprintOrSession(uint32_t fingerprint) {
  return fingerprint != 0 ? fingerprint : sessionCacheFingerprint();
}

}  // namespace papyrix
