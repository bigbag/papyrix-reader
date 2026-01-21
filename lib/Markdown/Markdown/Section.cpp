/**
 * MarkdownSection.cpp
 *
 * Markdown section implementation for page caching
 */

#include "Section.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include "Epub/Page.h"
#include "MarkdownParser.h"

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 1;  // v1: Initial markdown section format
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(uint8_t) + sizeof(uint8_t) +
                                 sizeof(uint8_t) + sizeof(bool) + sizeof(bool) + sizeof(uint16_t) + sizeof(uint16_t) +
                                 sizeof(uint16_t) + sizeof(uint32_t);
}  // namespace

uint32_t MarkdownSection::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    Serial.printf("[%lu] [MDS] File not open for writing page %d\n", millis(), pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    Serial.printf("[%lu] [MDS] Failed to serialize page %d\n", millis(), pageCount);
    return 0;
  }
  Serial.printf("[%lu] [MDS] Page %d processed\n", millis(), pageCount);

  pageCount++;
  return position;
}

void MarkdownSection::writeMarkdownSectionFileHeader(const RenderConfig& config) {
  if (!file) {
    Serial.printf("[%lu] [MDS] File not open for writing header\n", millis());
    return;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(config.fontId) + sizeof(config.lineCompression) +
                                   sizeof(config.indentLevel) + sizeof(config.spacingLevel) +
                                   sizeof(config.paragraphAlignment) + sizeof(config.hyphenation) +
                                   sizeof(config.showImages) + sizeof(config.viewportWidth) +
                                   sizeof(config.viewportHeight) + sizeof(pageCount) + sizeof(uint32_t),
                "Header size mismatch");
  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, config.fontId);
  serialization::writePod(file, config.lineCompression);
  serialization::writePod(file, config.indentLevel);
  serialization::writePod(file, config.spacingLevel);
  serialization::writePod(file, config.paragraphAlignment);
  serialization::writePod(file, config.hyphenation);
  serialization::writePod(file, config.showImages);
  serialization::writePod(file, config.viewportWidth);
  serialization::writePod(file, config.viewportHeight);
  serialization::writePod(file, pageCount);  // Placeholder for page count (will be initially 0 when written)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset
}

bool MarkdownSection::loadMarkdownSectionFile(const RenderConfig& config) {
  if (!SdMan.openFileForRead("MDS", filePath, file)) {
    return false;
  }

  // Match parameters
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION) {
      file.close();
      Serial.printf("[%lu] [MDS] Deserialization failed: Unknown version %u\n", millis(), version);
      clearCache();
      return false;
    }

    RenderConfig fileConfig;
    serialization::readPod(file, fileConfig.fontId);
    serialization::readPod(file, fileConfig.lineCompression);
    serialization::readPod(file, fileConfig.indentLevel);
    serialization::readPod(file, fileConfig.spacingLevel);
    serialization::readPod(file, fileConfig.paragraphAlignment);
    serialization::readPod(file, fileConfig.hyphenation);
    serialization::readPod(file, fileConfig.showImages);
    serialization::readPod(file, fileConfig.viewportWidth);
    serialization::readPod(file, fileConfig.viewportHeight);

    if (config != fileConfig) {
      file.close();
      Serial.printf("[%lu] [MDS] Deserialization failed: Parameters do not match\n", millis());
      clearCache();
      return false;
    }
  }

  serialization::readPod(file, pageCount);
  file.close();
  Serial.printf("[%lu] [MDS] Deserialization succeeded: %d pages\n", millis(), pageCount);
  return true;
}

bool MarkdownSection::clearCache() const {
  if (!SdMan.exists(filePath.c_str())) {
    Serial.printf("[%lu] [MDS] Cache does not exist, no action needed\n", millis());
    return true;
  }

  if (!SdMan.remove(filePath.c_str())) {
    Serial.printf("[%lu] [MDS] Failed to clear cache\n", millis());
    return false;
  }

  Serial.printf("[%lu] [MDS] Cache cleared successfully\n", millis());
  return true;
}

bool MarkdownSection::createMarkdownSectionFile(const RenderConfig& config,
                                                const std::function<void()>& progressSetupFn,
                                                const std::function<void(int)>& progressFn) {
  constexpr uint32_t MIN_SIZE_FOR_PROGRESS = 50 * 1024;  // 50KB

  // Create cache directory if it doesn't exist
  markdown->setupCacheDir();

  // Show progress for larger files
  if (progressSetupFn && markdown->getFileSize() >= MIN_SIZE_FOR_PROGRESS) {
    progressSetupFn();
  }

  if (!SdMan.openFileForWrite("MDS", filePath, file)) {
    return false;
  }
  writeMarkdownSectionFileHeader(config);
  std::vector<uint32_t> lut = {};

  MarkdownParser parser(
      markdown, renderer, config,
      [this, &lut](std::unique_ptr<Page> page) { lut.emplace_back(this->onPageComplete(std::move(page))); },
      progressFn);
  bool success = parser.parseAndBuildPages();

  if (!success) {
    Serial.printf("[%lu] [MDS] Failed to parse markdown and build pages\n", millis());
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  const uint32_t lutOffset = file.position();
  bool hasFailedLutRecords = false;

  // Write LUT
  for (const uint32_t& pos : lut) {
    if (pos == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(file, pos);
  }

  if (hasFailedLutRecords) {
    Serial.printf("[%lu] [MDS] Failed to write LUT due to invalid page positions\n", millis());
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  // Go back and write LUT offset
  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  file.close();
  return true;
}

std::unique_ptr<Page> MarkdownSection::loadPageFromMarkdownSectionFile() {
  if (!SdMan.openFileForRead("MDS", filePath, file)) {
    return nullptr;
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  file.seek(lutOffset + sizeof(uint32_t) * currentPage);
  uint32_t pagePos;
  serialization::readPod(file, pagePos);
  file.seek(pagePos);

  auto page = Page::deserialize(file);
  file.close();
  return page;
}
