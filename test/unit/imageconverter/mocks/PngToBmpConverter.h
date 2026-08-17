#pragma once

#include <Arduino.h>

#include <functional>

class FsFile;

class PngToBmpConverter {
  static bool writeMarker(Print& output) {
    static const uint8_t marker[] = {'P', 'N', 'G'};
    return output.write(marker, sizeof(marker)) == sizeof(marker);
  }

 public:
  static bool pngFileToBmpStreamWithSize(FsFile&, Print& output, int, int, bool = false, bool = false,
                                         const std::function<bool()>& = nullptr) {
    return writeMarker(output);
  }
};
