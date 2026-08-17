#pragma once

#include <cstdint>
#include <functional>
#include <string>

class FsFile;
class Print;

enum class ImageFormat : uint8_t {
  Unknown,
  Jpeg,
  Png,
  Bmp,
};

struct ImageConvertConfig {
  int maxWidth = 450;
  int maxHeight = 750;
  bool oneBit = false;
  bool requireDithering = false;
  const char* logTag = "IMG";
  std::function<bool()> shouldAbort = nullptr;
  std::function<bool(const std::string&)> validateOutput = nullptr;
};

class ImageConverter {
 public:
  virtual ~ImageConverter() = default;
  virtual bool convert(FsFile&, Print&, const ImageConvertConfig&) = 0;
  virtual const char* formatName() const = 0;
};

class ImageConverterFactory {
 public:
  static ImageFormat detectFormat(const std::string&) { return ImageFormat::Unknown; }
  static bool convertToBmp(const std::string&, const std::string&, const ImageConvertConfig& = {}) { return false; }
  static bool isSupported(const std::string&) { return false; }
};
