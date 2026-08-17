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
  virtual bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) = 0;
  virtual const char* formatName() const = 0;
};

class ImageConverterFactory {
 public:
  // Detect the actual source type from its file signature.
  static ImageFormat detectFormat(const std::string& filePath);

  // Convenience: convert file to BMP in one call (handles file I/O)
  static bool convertToBmp(const std::string& inputPath, const std::string& outputPath,
                           const ImageConvertConfig& config = {});

  // Check if format is supported
  static bool isSupported(const std::string& filePath);
};
