#include "Section.h"

#include <GfxRenderer.h>
#include <Html5Normalizer.h>
#include <Print.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include "Page.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 15;  // v15: Added CSS parser integration
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(uint8_t) + sizeof(uint8_t) +
                                 sizeof(uint8_t) + sizeof(bool) + sizeof(bool) + sizeof(uint16_t) + sizeof(uint16_t) +
                                 sizeof(uint16_t) + sizeof(uint32_t);
}  // namespace

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    Serial.printf("[%lu] [SCT] File not open for writing page %d\n", millis(), pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    Serial.printf("[%lu] [SCT] Failed to serialize page %d\n", millis(), pageCount);
    return 0;
  }
  Serial.printf("[%lu] [SCT] Page %d processed\n", millis(), pageCount);

  pageCount++;
  return position;
}

void Section::writeSectionFileHeader(const RenderConfig& config) {
  if (!file) {
    Serial.printf("[%lu] [SCT] File not open for writing header\n", millis());
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

bool Section::loadSectionFile(const RenderConfig& config) {
  if (!SdMan.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION) {
      file.close();
      Serial.printf("[%lu] [SCT] Deserialization failed: Unknown version %u\n", millis(), version);
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
      Serial.printf("[%lu] [SCT] Deserialization failed: Parameters do not match\n", millis());
      clearCache();
      return false;
    }
  }

  serialization::readPod(file, pageCount);
  file.close();
  Serial.printf("[%lu] [SCT] Deserialization succeeded: %d pages\n", millis(), pageCount);
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() const {
  if (!SdMan.exists(filePath.c_str())) {
    Serial.printf("[%lu] [SCT] Cache does not exist, no action needed\n", millis());
    return true;
  }

  if (!SdMan.remove(filePath.c_str())) {
    Serial.printf("[%lu] [SCT] Failed to clear cache\n", millis());
    return false;
  }

  Serial.printf("[%lu] [SCT] Cache cleared successfully\n", millis());
  return true;
}

bool Section::createSectionFile(const RenderConfig& config, const std::function<void()>& progressSetupFn,
                                const std::function<void(int)>& progressFn) {
  const unsigned long sectionStartMs = millis();
  constexpr uint32_t MIN_SIZE_FOR_PROGRESS = 50 * 1024;  // 50KB
  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directories if they don't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    SdMan.mkdir(sectionsDir.c_str());
    const auto imagesDir = epub->getCachePath() + "/images";
    SdMan.mkdir(imagesDir.c_str());
  }

  // Derive chapter base path for resolving relative image paths
  std::string chapterBasePath;
  {
    size_t lastSlash = localPath.rfind('/');
    if (lastSlash != std::string::npos) {
      chapterBasePath = localPath.substr(0, lastSlash + 1);
    }
  }
  const std::string imageCachePath = config.showImages ? (epub->getCachePath() + "/images") : "";

  // Retry logic for SD card timing issues
  bool success = false;
  uint32_t fileSize = 0;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    if (attempt > 0) {
      Serial.printf("[%lu] [SCT] Retrying stream (attempt %d)...\n", millis(), attempt + 1);
      delay(50);  // Brief delay before retry
    }

    // Remove any incomplete file from previous attempt before retrying
    if (SdMan.exists(tmpHtmlPath.c_str())) {
      SdMan.remove(tmpHtmlPath.c_str());
    }

    FsFile tmpHtml;
    if (!SdMan.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
      continue;
    }
    success = epub->readItemContentsToStream(localPath, tmpHtml, 4096);
    fileSize = tmpHtml.size();
    tmpHtml.close();

    // If streaming failed, remove the incomplete file immediately
    if (!success && SdMan.exists(tmpHtmlPath.c_str())) {
      SdMan.remove(tmpHtmlPath.c_str());
      Serial.printf("[%lu] [SCT] Removed incomplete temp file after failed attempt\n", millis());
    }
  }

  if (!success) {
    Serial.printf("[%lu] [SCT] Failed to stream item contents to temp file after retries\n", millis());
    return false;
  }

  Serial.printf("[%lu] [SCT] Streamed temp HTML to %s (%d bytes)\n", millis(), tmpHtmlPath.c_str(), fileSize);

  // Normalize HTML5 void elements to XHTML self-closing format for Expat parser
  const auto normalizedPath = epub->getCachePath() + "/.norm_" + std::to_string(spineIndex) + ".html";
  std::string parseHtmlPath = tmpHtmlPath;
  if (html5::normalizeVoidElements(tmpHtmlPath, normalizedPath)) {
    parseHtmlPath = normalizedPath;
    Serial.printf("[%lu] [SCT] Normalized HTML5 void elements\n", millis());
  } else {
    Serial.printf("[%lu] [SCT] Failed to normalize HTML, continuing with original\n", millis());
  }

  // Only show progress bar for larger chapters where rendering overhead is worth it
  if (progressSetupFn && fileSize >= MIN_SIZE_FOR_PROGRESS) {
    progressSetupFn();
  }

  if (!SdMan.openFileForWrite("SCT", filePath, file)) {
    SdMan.remove(tmpHtmlPath.c_str());
    SdMan.remove(normalizedPath.c_str());
    return false;
  }
  writeSectionFileHeader(config);
  std::vector<uint32_t> lut = {};

  // Create readItemFn callback for extracting images from EPUB
  auto readItemFn = [this](const std::string& href, Print& out, size_t chunkSize) -> bool {
    return epub->readItemContentsToStream(href, out, chunkSize);
  };

  ChapterHtmlSlimParser visitor(
      parseHtmlPath, renderer, config,
      [this, &lut](std::unique_ptr<Page> page) { lut.emplace_back(this->onPageComplete(std::move(page))); }, progressFn,
      chapterBasePath, imageCachePath, readItemFn, epub->getCssParser());
  success = visitor.parseAndBuildPages();

  SdMan.remove(tmpHtmlPath.c_str());
  SdMan.remove(normalizedPath.c_str());
  if (!success) {
    Serial.printf("[%lu] [SCT] Failed to parse XML and build pages\n", millis());
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
    Serial.printf("[%lu] [SCT] Failed to write LUT due to invalid page positions\n", millis());
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  // Go back and write LUT offset
  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  file.close();

  Serial.printf("[%lu] [PERF] Section creation: %lu ms (%d pages)\n", millis(), millis() - sectionStartMs, pageCount);

  // Clear word width cache to prevent unbounded memory growth across sections
  renderer.clearWidthCache();

  return true;
}

std::unique_ptr<Page> Section::loadPageFromSectionFile() {
  if (!SdMan.openFileForRead("SCT", filePath, file)) {
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
