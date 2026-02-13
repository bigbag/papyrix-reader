#include "GifToBmpConverter.h"

#include <HardwareSerial.h>
#include <SdFat.h>

#include "TinyGifDecoder.h"

bool GifToBmpConverter::gifFileToBmpStream(FsFile& input, Print& output, int maxWidth, int maxHeight) {
  return gifFileToBmpStreamWithSize(input, output, maxWidth, maxHeight, nullptr);
}

bool GifToBmpConverter::gifFileToBmpStreamWithSize(FsFile& input, Print& output, int maxWidth, int maxHeight,
                                                   std::function<bool()> shouldAbort) {
  // Use TinyGifDecoder for static GIF images
  size_t fileSize = input.size();
  if (fileSize > 200 * 1024) {
    Serial.printf("[GIF] ERROR: File too large (%zu bytes)\n", fileSize);
    return false;
  }

  uint8_t* fileBuffer = (uint8_t*)malloc(fileSize);
  if (!fileBuffer) {
    Serial.printf("[GIF] ERROR: Failed to allocate file buffer\n");
    return false;
  }

  size_t bytesRead = input.read(fileBuffer, fileSize);
  if (bytesRead != fileSize) {
    Serial.printf("[GIF] ERROR: Read failed (%zu/%zu bytes)\n", bytesRead, fileSize);
    free(fileBuffer);
    return false;
  }

  bool result = TinyGifDecoder::decodeGifToBmp(fileBuffer, fileSize, output, maxWidth, maxHeight, shouldAbort);
  free(fileBuffer);

  return result;
}

bool GifToBmpConverter::gifFileToBmpStreamQuick(FsFile& input, Print& output, int maxWidth, int maxHeight) {
  // Quick mode is same as normal for GIF
  return gifFileToBmpStreamWithSize(input, output, maxWidth, maxHeight, nullptr);
}