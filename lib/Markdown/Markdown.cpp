/**
 * Markdown.cpp
 *
 * Markdown file handler implementation for Papyrix Reader
 */

#include "Markdown.h"

#include <FsHelpers.h>
#include <HardwareSerial.h>
#include <JpegToBmpConverter.h>
#include <SDCardManager.h>

Markdown::Markdown(std::string filepath, const std::string& cacheDir)
    : filepath(std::move(filepath)), fileSize(0), loaded(false) {
  // Create cache key based on filepath (same as Epub/Xtc/Txt)
  cachePath = cacheDir + "/md_" + std::to_string(std::hash<std::string>{}(this->filepath));

  // Extract title from filename
  size_t lastSlash = this->filepath.find_last_of('/');
  size_t lastDot = this->filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    title = this->filepath.substr(lastSlash);
  } else {
    title = this->filepath.substr(lastSlash, lastDot - lastSlash);
  }
}

bool Markdown::load() {
  Serial.printf("[%lu] [MD ] Loading Markdown: %s\n", millis(), filepath.c_str());

  if (!SdMan.exists(filepath.c_str())) {
    Serial.printf("[%lu] [MD ] File does not exist\n", millis());
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("MD ", filepath, file)) {
    Serial.printf("[%lu] [MD ] Failed to open file\n", millis());
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;
  Serial.printf("[%lu] [MD ] Loaded Markdown: %s (%zu bytes)\n", millis(), filepath.c_str(), fileSize);
  return true;
}

bool Markdown::clearCache() const {
  if (!SdMan.exists(cachePath.c_str())) {
    Serial.printf("[%lu] [MD ] Cache does not exist, no action needed\n", millis());
    return true;
  }

  if (!SdMan.removeDir(cachePath.c_str())) {
    Serial.printf("[%lu] [MD ] Failed to clear cache\n", millis());
    return false;
  }

  Serial.printf("[%lu] [MD ] Cache cleared successfully\n", millis());
  return true;
}

void Markdown::setupCacheDir() const {
  if (SdMan.exists(cachePath.c_str())) {
    return;
  }

  // Create directories recursively
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      SdMan.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  SdMan.mkdir(cachePath.c_str());
}

std::string Markdown::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

std::string Markdown::findCoverImage() const {
  // Extract directory path
  size_t lastSlash = filepath.find_last_of('/');
  std::string dirPath = (lastSlash == std::string::npos) ? "/" : filepath.substr(0, lastSlash);
  if (dirPath.empty()) dirPath = "/";

  // Extract base filename without extension
  std::string baseName = title;

  // Priority 1: Image matching Markdown filename (e.g., README.jpg for README.md)
  const char* extensions[] = {".jpg", ".jpeg", ".bmp"};
  for (const char* ext : extensions) {
    std::string imagePath = dirPath + "/" + baseName + ext;
    if (SdMan.exists(imagePath.c_str())) {
      Serial.printf("[%lu] [MD ] Found cover image: %s\n", millis(), imagePath.c_str());
      return imagePath;
    }
  }

  // Priority 2: Generic cover image in same directory
  for (const char* ext : extensions) {
    std::string imagePath = dirPath + "/cover" + ext;
    if (SdMan.exists(imagePath.c_str())) {
      Serial.printf("[%lu] [MD ] Found cover image: %s\n", millis(), imagePath.c_str());
      return imagePath;
    }
  }

  return "";
}

bool Markdown::generateCoverBmp() const {
  // Already generated
  if (SdMan.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  // Find a cover image
  std::string coverImagePath = findCoverImage();
  if (coverImagePath.empty()) {
    Serial.printf("[%lu] [MD ] No cover image found\n", millis());
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Check if it's a BMP file (just copy)
  if (coverImagePath.length() >= 4) {
    std::string ext = coverImagePath.substr(coverImagePath.length() - 4);
    if (ext == ".bmp") {
      // Copy BMP file
      FsFile src, dst;
      if (!SdMan.openFileForRead("MD ", coverImagePath, src)) {
        Serial.printf("[%lu] [MD ] Failed to open source BMP\n", millis());
        return false;
      }
      if (!SdMan.openFileForWrite("MD ", getCoverBmpPath(), dst)) {
        src.close();
        Serial.printf("[%lu] [MD ] Failed to create destination BMP\n", millis());
        return false;
      }

      uint8_t buffer[512];
      while (src.available()) {
        size_t bytesRead = src.read(buffer, sizeof(buffer));
        dst.write(buffer, bytesRead);
      }

      src.close();
      dst.close();
      Serial.printf("[%lu] [MD ] Copied cover BMP: %s\n", millis(), getCoverBmpPath().c_str());
      return true;
    }
  }

  // Convert JPG to BMP
  FsFile jpegFile;
  if (!SdMan.openFileForRead("MD ", coverImagePath, jpegFile)) {
    Serial.printf("[%lu] [MD ] Failed to open JPEG file\n", millis());
    return false;
  }

  FsFile bmpFile;
  if (!SdMan.openFileForWrite("MD ", getCoverBmpPath(), bmpFile)) {
    jpegFile.close();
    Serial.printf("[%lu] [MD ] Failed to create BMP file\n", millis());
    return false;
  }

  bool success = JpegToBmpConverter::jpegFileToBmpStream(jpegFile, bmpFile);

  jpegFile.close();
  bmpFile.close();

  if (success) {
    Serial.printf("[%lu] [MD ] Generated cover BMP: %s\n", millis(), getCoverBmpPath().c_str());
  } else {
    Serial.printf("[%lu] [MD ] Failed to convert JPEG to BMP\n", millis());
    SdMan.remove(getCoverBmpPath().c_str());
  }

  return success;
}

size_t Markdown::readContent(uint8_t* buffer, size_t offset, size_t length) const {
  if (!loaded) {
    return 0;
  }

  FsFile file;
  if (!SdMan.openFileForRead("MD ", filepath, file)) {
    return 0;
  }

  if (offset > 0) {
    file.seek(offset);
  }

  size_t bytesRead = file.read(buffer, length);
  file.close();

  return bytesRead;
}
