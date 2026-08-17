#pragma once

#include <Arduino.h>

#include <functional>

class FsFile;

class JpegToBmpConverter {
  static bool writeMarker(Print& output);

 public:
  static bool jpegFileToBmpStream(FsFile&, Print& output) { return writeMarker(output); }
  static bool jpegFileToBmpStreamWithSize(FsFile&, Print& output, int, int,
                                          const std::function<bool()>& = nullptr) {
    return writeMarker(output);
  }
  static bool jpegFileTo1BitBmpStream(FsFile&, Print& output) { return writeMarker(output); }
  static bool jpegFileTo1BitBmpStreamWithSize(FsFile&, Print& output, int, int,
                                              const std::function<bool()>& = nullptr, bool = false) {
    return writeMarker(output);
  }
};

inline bool JpegToBmpConverter::writeMarker(Print& output) {
  static const uint8_t marker[] = {'J', 'P', 'E', 'G'};
  return output.write(marker, sizeof(marker)) == sizeof(marker);
}
