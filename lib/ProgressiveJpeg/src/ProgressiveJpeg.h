#pragma once

#include <TargetConfig.h>

#if PAPYRIX_CAP_PROGRESSIVE_JPEG
#include <SdFat.h>

#include <cstddef>
#include <cstdint>

namespace ProgressiveJpeg {

enum class Result : uint8_t { Ok, Unsupported, InvalidData, IoError, OutOfMemory, Aborted, Stopped };

struct ComponentInfo {
  uint8_t id = 0;
  uint8_t horizontal = 1;
  uint8_t vertical = 1;
  uint8_t quantizer = 0;
};

struct ImageInfo {
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t componentCount = 0;
  uint8_t maxHorizontal = 1;
  uint8_t maxVertical = 1;
  ComponentInfo components[3]{};
};

using AbortCallback = bool (*)(void* user);
using BandCallback = bool (*)(void* user, uint16_t y, const uint8_t* gray, uint16_t width, uint16_t rows,
                              uint16_t stride);

struct DecodeOptions {
  uint8_t* workspace = nullptr;
  size_t workspaceSize = 0;
  AbortCallback shouldAbort = nullptr;
  void* abortUser = nullptr;
};

Result probe(FsFile& file, ImageInfo& info, AbortCallback shouldAbort = nullptr, void* abortUser = nullptr);
size_t workspaceBytes(const ImageInfo& info);
Result decode(FsFile& file, const ImageInfo& info, const DecodeOptions& options, BandCallback callback, void* user);

}  // namespace ProgressiveJpeg
#endif
