/**
 * Fb2.cpp
 *
 * FictionBook 2.0 XML e-book handler implementation for Papyrix Reader
 */

#include "Fb2.h"

#include <CoverHelpers.h>
#include <EncodingDetector.h>
#include <ExpatEncodingHandler.h>
#include <FsHelpers.h>
#include <Logging.h>

#include "Base64Decoder.h"

#define TAG "FB2"
#include <SDCardManager.h>
#include <Serialization.h>

#include <cstring>

namespace {
constexpr uint8_t kMetaCacheVersion = 4;
constexpr char kMetaCacheFile[] = "/meta.bin";
}  // namespace

std::string Fb2::metaCachePath() const { return cachePath + kMetaCacheFile; }

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

  // Free TOC strings, rebuild as compact LUT from cache
  std::vector<TocItem>().swap(tocItems_);
  if (!loadMetaCache()) {
    LOG_ERR(TAG, "Failed to reload meta cache for LUT");
  }

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

  // Skip binary content (base64-encoded images), but capture cover content-type
  if (strcmp(tag, "binary") == 0) {
    if (!self->coverRef.empty() && atts) {
      bool isTargetBinary = false;
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "id") == 0 && atts[i + 1] && self->coverRef == atts[i + 1]) {
          isTargetBinary = true;
          break;
        }
      }
      if (isTargetBinary) {
        self->coverBinaryOffset_ = XML_GetCurrentByteIndex(self->xmlParser_);
        for (int i = 0; atts[i]; i += 2) {
          if (strcmp(atts[i], "content-type") == 0 && atts[i + 1]) {
            self->coverContentType = atts[i + 1];
            break;
          }
        }
      }
    }
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
  } else if (strcmp(tag, "lang") == 0 && self->inTitleInfo) {
    self->inLang = true;
    self->language.clear();
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
            self->coverRef = attrValue + 1;
          } else {
            self->coverRef = attrValue;
          }
          LOG_INF(TAG, "Found cover reference: %s", self->coverRef.c_str());
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
  } else if (strcmp(tag, "lang") == 0 && self->inLang) {
    self->inLang = false;
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
  } else if (self->inLang) {
    self->language.append(s, len);
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

  constexpr size_t kChunkSize = 4096;
  uint8_t buffer[kChunkSize];

  // Peek first chunk for encoding detection. If bytes are actually UTF-8,
  // force Expat to UTF-8 even when the declaration says CP1251/KOI8-R
  // (mis-declared files). Genuine single-byte encodings fall through to the
  // expatUnknownEncodingHandler path below.
  const size_t peekBytes = file.read(buffer, kChunkSize);
  const char* explicitEncoding = nullptr;
  size_t bomSkip = 0;
  if (peekBytes > 0 && detectEncoding(buffer, peekBytes, bomSkip) == Encoding::Utf8) {
    explicitEncoding = "UTF-8";
  }
  file.seek(bomSkip);

  xmlParser_ = XML_ParserCreate(explicitEncoding);
  if (!xmlParser_) {
    LOG_ERR(TAG, "Failed to create XML parser");
    file.close();
    return false;
  }

  XML_SetUserData(xmlParser_, this);
  XML_SetUnknownEncodingHandler(xmlParser_, expatUnknownEncodingHandler, nullptr);
  XML_SetElementHandler(xmlParser_, startElement, endElement);
  XML_SetCharacterDataHandler(xmlParser_, characterData);

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

  // Trim language whitespace
  while (!language.empty() && isspace(static_cast<unsigned char>(language.back()))) {
    language.pop_back();
  }
  while (!language.empty() && isspace(static_cast<unsigned char>(language.front()))) {
    language.erase(language.begin());
  }

  LOG_INF(TAG, "XML parsing complete: title='%s', author='%s', lang='%s'", title.c_str(), author.c_str(),
          language.c_str());
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

bool Fb2::extractEmbeddedCover(const std::string& outputPath) const {
  if (coverRef.empty() || coverBinaryOffset_ < 0) return false;

  FsFile fb2File;
  if (!SdMan.openFileForRead("FB2", filepath, fb2File)) {
    return false;
  }

  if (!fb2File.seek(static_cast<uint32_t>(coverBinaryOffset_))) {
    fb2File.close();
    return false;
  }

  // Scan forward past '>' that closes the <binary ...> start tag.
  // FB2 <binary> attributes are only id and content-type (simple filenames/MIME types),
  // so '>' will not appear inside attribute values in well-formed FB2 files.
  constexpr size_t kBufSize = 256;
  uint8_t buf[kBufSize];
  bool foundTagEnd = false;
  int scanLimit = 512;

  while (!foundTagEnd && scanLimit > 0 && fb2File.available() > 0) {
    size_t toRead = static_cast<size_t>(scanLimit) < kBufSize ? static_cast<size_t>(scanLimit) : kBufSize;
    size_t n = fb2File.read(buf, toRead);
    if (n == 0) break;
    scanLimit -= static_cast<int>(n);

    for (size_t i = 0; i < n; i++) {
      if (buf[i] == '>') {
        // Seek back to just after '>'
        fb2File.seek(fb2File.position() - (n - i - 1));
        foundTagEnd = true;
        break;
      }
    }
  }

  if (!foundTagEnd) {
    fb2File.close();
    return false;
  }

  FsFile outFile;
  if (!SdMan.openFileForWrite("FB2", outputPath, outFile)) {
    fb2File.close();
    return false;
  }

  // Read base64 content until '</binary>' terminator.
  // '<' cannot appear in base64, so the first '<' marks end of content.
  Base64Decoder decoder;
  bool done = false;
  bool success = false;

  auto writeCallback = [&outFile](const uint8_t* data, size_t sz) { return outFile.write(data, sz) == sz; };

  while (!done && fb2File.available() > 0) {
    size_t n = fb2File.read(buf, kBufSize);
    if (n == 0) break;

    // Find '<' that starts the closing tag
    size_t feedLen = n;
    for (size_t i = 0; i < n; i++) {
      if (buf[i] == '<') {
        feedLen = i;
        done = true;
        break;
      }
    }

    if (feedLen > 0) {
      decoder.feed(reinterpret_cast<const char*>(buf), static_cast<int>(feedLen), writeCallback);
    }

    if (decoder.failed()) break;
  }

  if (done && !decoder.failed()) {
    success = decoder.finish(writeCallback);
  }

  outFile.close();
  fb2File.close();

  if (!success) {
    SdMan.remove(outputPath.c_str());
  }

  LOG_INF(TAG, "Cover extraction %s", success ? "succeeded" : "failed");
  return success;
}

bool Fb2::generateCoverBmp(bool use1BitDithering) const {
  const auto coverBmpPath = getCoverBmpPath();
  const auto failedMarkerPath = cachePath + "/.cover.failed";

  // Already generated
  if (SdMan.exists(coverBmpPath.c_str())) {
    return true;
  }

  // Previously failed, don't retry
  if (SdMan.exists(failedMarkerPath.c_str())) {
    return false;
  }

  // Find a cover image (external file in same directory)
  std::string coverImagePath = findCoverImage();

  // Try extracting embedded cover if no external image found
  std::string tmpCoverPath;
  if (coverImagePath.empty() && !coverRef.empty()) {
    std::string ext = ".jpg";
    if (coverContentType == "image/png") {
      ext = ".png";
    } else if (coverContentType == "image/bmp") {
      ext = ".bmp";
    }
    tmpCoverPath = cachePath + "/.tmp_cover" + ext;
    setupCacheDir();
    if (extractEmbeddedCover(tmpCoverPath)) {
      coverImagePath = tmpCoverPath;
    }
  }

  if (coverImagePath.empty()) {
    LOG_INF(TAG, "No cover image found");
    FsFile marker;
    if (SdMan.openFileForWrite("FB2", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Convert to BMP using shared helper
  const bool success = CoverHelpers::convertImageToBmp(coverImagePath, coverBmpPath, "FB2", use1BitDithering);

  // Clean up temp file
  if (!tmpCoverPath.empty()) {
    SdMan.remove(tmpCoverPath.c_str());
  }

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
  FsFile file;
  if (!SdMan.openFileForRead("FB2", metaCachePath(), file)) {
    return false;
  }

  uint8_t version;
  if (!serialization::readPodChecked(file, version) || version != kMetaCacheVersion) {
    LOG_ERR(TAG, "Meta cache version mismatch");
    file.close();
    return false;
  }

  if (!serialization::readString(file, title) || !serialization::readString(file, author) ||
      !serialization::readString(file, coverRef) || !serialization::readString(file, language) ||
      !serialization::readString(file, coverContentType)) {
    LOG_ERR(TAG, "Failed to read meta cache strings");
    file.close();
    return false;
  }

  int64_t cachedOffset;
  if (!serialization::readPodChecked(file, cachedOffset)) {
    file.close();
    return false;
  }
  coverBinaryOffset_ = cachedOffset;

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

  // Build compact LUT: record file offset for each TOC entry, skip the actual data
  tocItemCount_ = tocItemCount;
  // Release old capacity before reserving new (swap idiom clears capacity from previous load)
  std::vector<uint32_t>().swap(tocLut_);
  tocLut_.reserve(tocItemCount);
  for (uint16_t i = 0; i < tocItemCount; i++) {
    tocLut_.push_back(static_cast<uint32_t>(file.position()));
    int16_t dummyIdx;
    if (!serialization::skipString(file) || !serialization::readPodChecked(file, dummyIdx)) {
      tocLut_.clear();
      tocItemCount_ = 0;
      file.close();
      return false;
    }
  }

  file.close();
  return true;
}

bool Fb2::saveMetaCache() const {
  setupCacheDir();

  FsFile file;
  if (!SdMan.openFileForWrite("FB2", metaCachePath(), file)) {
    LOG_ERR(TAG, "Failed to create meta cache");
    return false;
  }

  serialization::writePod(file, kMetaCacheVersion);
  serialization::writeString(file, title);
  serialization::writeString(file, author);
  serialization::writeString(file, coverRef);
  serialization::writeString(file, language);
  serialization::writeString(file, coverContentType);
  serialization::writePod(file, coverBinaryOffset_);

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

Fb2::TocItem Fb2::getTocItem(uint16_t index) const {
  TocItem item;
  if (index >= tocItemCount_) return item;

  FsFile file;
  if (!SdMan.openFileForRead("FB2", metaCachePath(), file)) return item;

  file.seek(tocLut_[index]);
  serialization::readString(file, item.title);
  int16_t idx;
  if (serialization::readPodChecked(file, idx)) {
    item.sectionIndex = idx;
  }
  file.close();
  return item;
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
