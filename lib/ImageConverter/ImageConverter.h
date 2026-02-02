#pragma once

#include <string>

class FsFile;
class Print;

struct ImageConvertConfig {
  int maxWidth = 480;
  int maxHeight = 800;
  bool oneBit = false;
  bool quickMode = false;  // Fast preview: simple threshold instead of dithering
  const char* logTag = "IMG";
};

class ImageConverter {
 public:
  virtual ~ImageConverter() = default;
  virtual bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) = 0;
  virtual const char* formatName() const = 0;
};

class ImageConverterFactory {
 public:
  // Returns appropriate converter based on file extension (or nullptr if unsupported)
  static ImageConverter* getConverter(const std::string& filePath);

  // Convenience: convert file to BMP in one call (handles file I/O)
  static bool convertToBmp(const std::string& inputPath, const std::string& outputPath,
                           const ImageConvertConfig& config = {});

  // Check if format is supported
  static bool isSupported(const std::string& filePath);
};
