/**
 * Fb2.cpp
 *
 * FictionBook 2.0 XML e-book handler implementation for Papyrix Reader
 */

#include "Fb2.h"

#include <CoverHelpers.h>
#include <FsHelpers.h>
#include <Logging.h>

#define TAG "FB2"
#include <SDCardManager.h>
#include <Serialization.h>

#include <cstring>

namespace {
constexpr uint8_t kMetaCacheVersion = 2;
constexpr char kMetaCacheFile[] = "/meta.bin";
}  // namespace

Fb2::Fb2(std::string filepath, const std::string& cacheDir)
    : filepath(std::move(filepath)), fileSize(0), loaded(false) {
  // Create cache key based on filepath (same as Epub/Xtc/Txt)
  cachePath = cacheDir + "/fb2_" + std::to_string(std::hash<std::string>{}(this->filepath));

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

Fb2::~Fb2() {
  if (xmlParser_) {
    XML_ParserFree(xmlParser_);
    xmlParser_ = nullptr;
  }
}

bool Fb2::load() {
  LOG_INF(TAG, "Loading FB2: %s", filepath.c_str());

  if (!SdMan.exists(filepath.c_str())) {
    LOG_ERR(TAG, "File does not exist");
    return false;
  }

  // Try loading from metadata cache first
  if (loadMetaCache()) {
    loaded = true;
    LOG_INF(TAG, "Loaded from cache: %s (title: '%s', author: '%s')", filepath.c_str(), title.c_str(), author.c_str());
    return true;
  }

  FsFile file;
  if (!SdMan.openFileForRead("FB2", filepath, file)) {
    LOG_ERR(TAG, "Failed to open file");
    return false;
  }

  fileSize = file.size();
  file.close();

  // Stream-parse in chunks (file may exceed available RAM)
  if (!parseXmlStream()) {
    LOG_ERR(TAG, "Failed to parse XML");
    return false;
  }

  saveMetaCache();

  loaded = true;
  LOG_INF(TAG, "Loaded FB2: %s (title: '%s', author: '%s')", filepath.c_str(), title.c_str(), author.c_str());
  return true;
}

void XMLCALL Fb2::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<Fb2*>(userData);

  self->depth++;

  // Prevent stack overflow from deeply nested XML
  if (self->depth >= 100) {
    return;
  }

  // Skip content inside <binary> tags (embedded images)
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  // FB2 uses namespaces, strip prefix if present
  const char* tag = strrchr(name, ':');
  if (tag) {
    tag++;
  } else {
    tag = name;
  }

  // Skip binary content (base64-encoded images)
  if (strcmp(tag, "binary") == 0) {
    self->skipUntilDepth = self->depth - 1;
    return;
  }

  // Track <title-info> to only collect metadata from it (not <document-info>)
  if (strcmp(tag, "title-info") == 0) {
    self->inTitleInfo = true;
  }

  // Description / Metadata (only from <title-info>)
  if (strcmp(tag, "book-title") == 0 && self->inTitleInfo) {
    self->inBookTitle = true;
    self->title.clear();
  } else if (strcmp(tag, "author") == 0 && self->inTitleInfo) {
    self->inAuthor = true;
    self->currentAuthorFirst.clear();
    self->currentAuthorLast.clear();
  } else if (strcmp(tag, "first-name") == 0 && self->inAuthor) {
    self->inFirstName = true;
  } else if (strcmp(tag, "last-name") == 0 && self->inAuthor) {
    self->inLastName = true;
  } else if (strcmp(tag, "coverpage") == 0) {
    self->inCoverPage = true;
  } else if (strcmp(tag, "image") == 0 && self->inCoverPage) {
    // Look for l:href or href attribute
    if (atts) {
      for (int i = 0; atts[i]; i += 2) {
        const char* attrName = atts[i];
        const char* attrValue = atts[i + 1];

        // Handle both l:href and href
        const char* attr = strrchr(attrName, ':');
        if (attr)
          attr++;
        else
          attr = attrName;

        if ((strcmp(attr, "href") == 0 || strcmp(attrName, "l:href") == 0) && attrValue) {
          // Store the reference (remove # prefix)
          if (attrValue[0] == '#') {
            self->coverPath = attrValue + 1;
          } else {
            self->coverPath = attrValue;
          }
          LOG_INF(TAG, "Found cover reference: %s", self->coverPath.c_str());
          break;
        }
      }
    }
  } else if (strcmp(tag, "body") == 0) {
    self->bodyCount_++;
    self->inBody = (self->bodyCount_ == 1);
  } else if (strcmp(tag, "section") == 0 && self->inBody) {
    self->sectionCounter_++;
  } else if (strcmp(tag, "title") == 0 && self->inBody && self->sectionCounter_ > 0) {
    self->inSectionTitle_ = true;
    self->sectionTitleDepth_ = self->depth;
    self->currentSectionTitle_.clear();
  }
}

void XMLCALL Fb2::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<Fb2*>(userData);

  // FB2 uses namespaces, strip prefix if present
  const char* tag = strrchr(name, ':');
  if (tag) {
    tag++;
  } else {
    tag = name;
  }

  if (strcmp(tag, "title-info") == 0) {
    self->inTitleInfo = false;
  }

  if (strcmp(tag, "book-title") == 0) {
    self->inBookTitle = false;
  } else if (strcmp(tag, "first-name") == 0) {
    self->inFirstName = false;
  } else if (strcmp(tag, "last-name") == 0) {
    self->inLastName = false;
  } else if (strcmp(tag, "author") == 0 && self->inAuthor) {
    // Combine first and last name for author
    std::string fullAuthor;
    if (!self->currentAuthorFirst.empty()) {
      fullAuthor = self->currentAuthorFirst;
      if (!self->currentAuthorLast.empty()) {
        fullAuthor += " ";
      }
    }
    fullAuthor += self->currentAuthorLast;

    if (!fullAuthor.empty()) {
      if (!self->author.empty()) {
        self->author += ", ";
      }
      self->author += fullAuthor;
    }

    self->inAuthor = false;
    self->currentAuthorFirst.clear();
    self->currentAuthorLast.clear();
  } else if (strcmp(tag, "coverpage") == 0) {
    self->inCoverPage = false;
  } else if (strcmp(tag, "binary") == 0) {
    // Exit binary tag - stop skipping
    self->skipUntilDepth = INT_MAX;
  } else if (strcmp(tag, "body") == 0) {
    self->inBody = false;
  } else if (strcmp(tag, "title") == 0 && self->inSectionTitle_ && self->depth == self->sectionTitleDepth_) {
    self->inSectionTitle_ = false;

    // Trim whitespace and replace newlines with spaces
    std::string& t = self->currentSectionTitle_;
    for (size_t i = 0; i < t.size(); i++) {
      if (t[i] == '\n' || t[i] == '\r') {
        t[i] = ' ';
      }
    }
    // Trim leading whitespace
    size_t start = 0;
    while (start < t.size() && isspace(static_cast<unsigned char>(t[start]))) {
      start++;
    }
    // Trim trailing whitespace
    size_t end = t.size();
    while (end > start && isspace(static_cast<unsigned char>(t[end - 1]))) {
      end--;
    }
    if (start > 0 || end < t.size()) {
      t = t.substr(start, end - start);
    }

    if (!t.empty()) {
      self->tocItems_.push_back({t, self->sectionCounter_ - 1});
    }
  }

  self->depth--;
}

void XMLCALL Fb2::characterData(void* userData, const XML_Char* s, int len) {
  auto* self = static_cast<Fb2*>(userData);

  // Skip if inside binary tags
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  // Collect section title text for TOC
  if (self->inSectionTitle_) {
    self->currentSectionTitle_.append(s, len);
  }

  // Extract metadata based on current context
  if (self->inBookTitle) {
    self->title.append(s, len);
  } else if (self->inFirstName) {
    self->currentAuthorFirst.append(s, len);
  } else if (self->inLastName) {
    self->currentAuthorLast.append(s, len);
  }
}

bool Fb2::parseXmlStream() {
  LOG_INF(TAG, "Starting streaming XML parse");

  FsFile file;
  if (!SdMan.openFileForRead("FB2", filepath, file)) {
    return false;
  }

  xmlParser_ = XML_ParserCreate("UTF-8");
  if (!xmlParser_) {
    LOG_ERR(TAG, "Failed to create XML parser");
    file.close();
    return false;
  }

  XML_SetUserData(xmlParser_, this);
  XML_SetElementHandler(xmlParser_, startElement, endElement);
  XML_SetCharacterDataHandler(xmlParser_, characterData);

  constexpr size_t kChunkSize = 4096;
  uint8_t buffer[kChunkSize];
  bool success = true;

  while (file.available() > 0) {
    const size_t bytesRead = file.read(buffer, kChunkSize);
    if (bytesRead == 0) break;

    const int done = (file.available() == 0) ? 1 : 0;
    if (XML_Parse(xmlParser_, reinterpret_cast<const char*>(buffer), static_cast<int>(bytesRead), done) ==
        XML_STATUS_ERROR) {
      LOG_ERR(TAG, "XML parse error: %s", XML_ErrorString(XML_GetErrorCode(xmlParser_)));
      success = false;
      break;
    }
  }

  file.close();

  if (success) {
    postProcessMetadata();
  }

  XML_ParserFree(xmlParser_);
  xmlParser_ = nullptr;
  return success;
}

void Fb2::postProcessMetadata() {
  // Clean up title (remove newlines and extra whitespace)
  while (!title.empty() && isspace(static_cast<unsigned char>(title.back()))) {
    title.pop_back();
  }
  while (!title.empty() && isspace(static_cast<unsigned char>(title.front()))) {
    title.erase(title.begin());
  }

  // Replace newlines in title with spaces
  for (size_t i = 0; i < title.size(); i++) {
    if (title[i] == '\n' || title[i] == '\r') {
      title[i] = ' ';
    }
  }

  LOG_INF(TAG, "XML parsing complete: title='%s', author='%s'", title.c_str(), author.c_str());
}

bool Fb2::clearCache() const {
  if (!SdMan.exists(cachePath.c_str())) {
    LOG_INF(TAG, "Cache does not exist, no action needed");
    return true;
  }

  if (!SdMan.removeDir(cachePath.c_str())) {
    LOG_ERR(TAG, "Failed to clear cache");
    return false;
  }

  LOG_INF(TAG, "Cache cleared successfully");
  return true;
}

void Fb2::setupCacheDir() const {
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

std::string Fb2::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

std::string Fb2::findCoverImage() const {
  // Extract directory path
  size_t lastSlash = filepath.find_last_of('/');
  std::string dirPath = (lastSlash == std::string::npos) ? "/" : filepath.substr(0, lastSlash);
  if (dirPath.empty()) {
    dirPath = "/";
  }

  return CoverHelpers::findCoverImage(dirPath, title);
}

bool Fb2::generateCoverBmp(bool use1BitDithering) const {
  const auto coverPath = getCoverBmpPath();
  const auto failedMarkerPath = cachePath + "/.cover.failed";

  // Already generated
  if (SdMan.exists(coverPath.c_str())) {
    return true;
  }

  // Previously failed, don't retry
  if (SdMan.exists(failedMarkerPath.c_str())) {
    return false;
  }

  // Find a cover image
  std::string coverImagePath = findCoverImage();
  if (coverImagePath.empty()) {
    LOG_INF(TAG, "No cover image found");
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("FB2", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Convert to BMP using shared helper
  const bool success = CoverHelpers::convertImageToBmp(coverImagePath, coverPath, "FB2", use1BitDithering);
  if (!success) {
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("FB2", failedMarkerPath, marker)) {
      marker.close();
    }
  }
  return success;
}

std::string Fb2::getThumbBmpPath() const { return cachePath + "/thumb.bmp"; }

bool Fb2::generateThumbBmp() const {
  const auto thumbPath = getThumbBmpPath();
  const auto failedMarkerPath = cachePath + "/.thumb.failed";

  if (SdMan.exists(thumbPath.c_str())) {
    return true;
  }

  // Previously failed, don't retry
  if (SdMan.exists(failedMarkerPath.c_str())) {
    return false;
  }

  if (!SdMan.exists(getCoverBmpPath().c_str()) && !generateCoverBmp(true)) {
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("FB2", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  setupCacheDir();

  const bool success = CoverHelpers::generateThumbFromCover(getCoverBmpPath(), thumbPath, "FB2");
  if (!success) {
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("FB2", failedMarkerPath, marker)) {
      marker.close();
    }
  }
  return success;
}

bool Fb2::loadMetaCache() {
  const std::string metaPath = cachePath + kMetaCacheFile;
  FsFile file;
  if (!SdMan.openFileForRead("FB2", metaPath, file)) {
    return false;
  }

  uint8_t version;
  if (!serialization::readPodChecked(file, version) || version != kMetaCacheVersion) {
    LOG_ERR(TAG, "Meta cache version mismatch");
    file.close();
    return false;
  }

  if (!serialization::readString(file, title) || !serialization::readString(file, author) ||
      !serialization::readString(file, coverPath)) {
    LOG_ERR(TAG, "Failed to read meta cache strings");
    file.close();
    return false;
  }

  uint32_t cachedFileSize;
  if (!serialization::readPodChecked(file, cachedFileSize)) {
    file.close();
    return false;
  }
  fileSize = cachedFileSize;

  uint16_t sectionCount;
  if (!serialization::readPodChecked(file, sectionCount)) {
    file.close();
    return false;
  }
  sectionCounter_ = sectionCount;

  uint16_t tocItemCount;
  if (!serialization::readPodChecked(file, tocItemCount)) {
    file.close();
    return false;
  }

  tocItems_.clear();
  tocItems_.reserve(tocItemCount);
  for (uint16_t i = 0; i < tocItemCount; i++) {
    TocItem item;
    if (!serialization::readString(file, item.title)) {
      file.close();
      return false;
    }
    int16_t idx;
    if (!serialization::readPodChecked(file, idx)) {
      file.close();
      return false;
    }
    item.sectionIndex = idx;
    tocItems_.push_back(std::move(item));
  }

  file.close();
  return true;
}

bool Fb2::saveMetaCache() const {
  setupCacheDir();

  const std::string metaPath = cachePath + kMetaCacheFile;
  FsFile file;
  if (!SdMan.openFileForWrite("FB2", metaPath, file)) {
    LOG_ERR(TAG, "Failed to create meta cache");
    return false;
  }

  serialization::writePod(file, kMetaCacheVersion);
  serialization::writeString(file, title);
  serialization::writeString(file, author);
  serialization::writeString(file, coverPath);

  const uint32_t size32 = static_cast<uint32_t>(fileSize);
  serialization::writePod(file, size32);

  const uint16_t sectionCount = static_cast<uint16_t>(sectionCounter_);
  serialization::writePod(file, sectionCount);

  const uint16_t tocItemCount = static_cast<uint16_t>(tocItems_.size());
  serialization::writePod(file, tocItemCount);

  for (const auto& item : tocItems_) {
    serialization::writeString(file, item.title);
    const int16_t idx = static_cast<int16_t>(item.sectionIndex);
    serialization::writePod(file, idx);
  }

  file.close();
  LOG_INF(TAG, "Saved meta cache (%u TOC items)", tocItemCount);
  return true;
}

size_t Fb2::readContent(uint8_t* buffer, size_t offset, size_t length) const {
  if (!loaded) {
    return 0;
  }

  FsFile file;
  if (!SdMan.openFileForRead("FB2", filepath, file)) {
    return 0;
  }

  if (offset > 0) {
    file.seek(offset);
  }

  const size_t bytesRead = file.read(buffer, length);
  file.close();

  return bytesRead;
}
