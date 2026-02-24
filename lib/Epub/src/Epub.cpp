#include "Epub.h"

#include <CoverHelpers.h>
#include <FsHelpers.h>
#include <ImageConverter.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <ZipFile.h>

#define TAG "EPUB"

#include "../../src/config.h"
#include "Epub/parsers/ContainerParser.h"
#include "Epub/parsers/ContentOpfParser.h"
#include "Epub/parsers/TocNavParser.h"
#include "Epub/parsers/TocNcxParser.h"

bool Epub::findContentOpfFile(std::string* contentOpfFile) const {
  const auto containerPath = "META-INF/container.xml";
  size_t containerSize;

  // Get file size without loading it all into heap
  if (!getItemSize(containerPath, &containerSize)) {
    LOG_ERR(TAG, "Could not find or size META-INF/container.xml");
    return false;
  }

  ContainerParser containerParser(containerSize);

  if (!containerParser.setup()) {
    return false;
  }

  // Stream read (reusing your existing stream logic)
  if (!readItemContentsToStream(containerPath, containerParser, 512)) {
    LOG_ERR(TAG, "Could not read META-INF/container.xml");
    return false;
  }

  // Extract the result
  if (containerParser.fullPath.empty()) {
    LOG_ERR(TAG, "Could not find valid rootfile in container.xml");
    return false;
  }

  *contentOpfFile = std::move(containerParser.fullPath);
  return true;
}

bool Epub::parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata) {
  std::string contentOpfFilePath;
  if (!findContentOpfFile(&contentOpfFilePath)) {
    LOG_ERR(TAG, "Could not find content.opf in zip");
    return false;
  }

  contentBasePath = contentOpfFilePath.substr(0, contentOpfFilePath.find_last_of('/') + 1);

  LOG_INF(TAG, "Parsing content.opf: %s", contentOpfFilePath.c_str());

  size_t contentOpfSize;
  if (!getItemSize(contentOpfFilePath, &contentOpfSize)) {
    LOG_ERR(TAG, "Could not get size of content.opf");
    return false;
  }

  ContentOpfParser opfParser(getCachePath(), getBasePath(), contentOpfSize, bookMetadataCache.get());
  if (!opfParser.setup()) {
    LOG_ERR(TAG, "Could not setup content.opf parser");
    return false;
  }

  if (!readItemContentsToStream(contentOpfFilePath, opfParser, 1024)) {
    LOG_ERR(TAG, "Could not read content.opf");
    return false;
  }

  // Grab data from opfParser into epub
  bookMetadata.title = opfParser.title;
  bookMetadata.author = opfParser.author;
  bookMetadata.language = opfParser.language;
  bookMetadata.coverItemHref = opfParser.coverItemHref;
  bookMetadata.textReferenceHref = opfParser.textReferenceHref;

  if (!opfParser.tocNcxPath.empty()) {
    tocNcxItem = opfParser.tocNcxPath;
  }

  if (!opfParser.tocNavPath.empty()) {
    tocNavItem = opfParser.tocNavPath;
  }

  // Capture CSS files from manifest
  cssFiles_ = opfParser.getCssFiles();
  LOG_DBG(TAG, "Found %d CSS files in manifest", static_cast<int>(cssFiles_.size()));

  LOG_INF(TAG, "Successfully parsed content.opf");
  return true;
}

bool Epub::parseTocNcxFile() const {
  // the ncx file should have been specified in the content.opf file
  if (tocNcxItem.empty()) {
    LOG_DBG(TAG, "No ncx file specified");
    return false;
  }

  LOG_INF(TAG, "Parsing toc ncx file: %s", tocNcxItem.c_str());

  const auto tmpNcxPath = getCachePath() + "/toc.ncx";
  FsFile tempNcxFile;
  if (!SdMan.openFileForWrite("EBP", tmpNcxPath, tempNcxFile)) {
    return false;
  }
  if (!readItemContentsToStream(tocNcxItem, tempNcxFile, 1024)) {
    tempNcxFile.close();
    return false;
  }
  tempNcxFile.close();
  if (!SdMan.openFileForRead("EBP", tmpNcxPath, tempNcxFile)) {
    return false;
  }
  const auto ncxSize = tempNcxFile.size();

  TocNcxParser ncxParser(contentBasePath, ncxSize, bookMetadataCache.get());

  if (!ncxParser.setup()) {
    LOG_ERR(TAG, "Could not setup toc ncx parser");
    tempNcxFile.close();
    return false;
  }

  const auto ncxBuffer = static_cast<uint8_t*>(malloc(1024));
  if (!ncxBuffer) {
    LOG_ERR(TAG, "Could not allocate memory for toc ncx parser");
    tempNcxFile.close();
    return false;
  }

  while (tempNcxFile.available()) {
    const auto readSize = tempNcxFile.read(ncxBuffer, 1024);
    const auto processedSize = ncxParser.write(ncxBuffer, readSize);

    if (processedSize != readSize) {
      LOG_ERR(TAG, "Could not process all toc ncx data");
      free(ncxBuffer);
      tempNcxFile.close();
      return false;
    }
  }

  free(ncxBuffer);
  tempNcxFile.close();
  SdMan.remove(tmpNcxPath.c_str());

  LOG_INF(TAG, "Parsed TOC items");
  return true;
}

bool Epub::parseTocNavFile() const {
  // the nav file should have been specified in the content.opf file (EPUB 3)
  if (tocNavItem.empty()) {
    LOG_DBG(TAG, "No nav file specified");
    return false;
  }

  LOG_INF(TAG, "Parsing toc nav file: %s", tocNavItem.c_str());

  const auto tmpNavPath = getCachePath() + "/toc.nav";
  FsFile tempNavFile;
  if (!SdMan.openFileForWrite("EBP", tmpNavPath, tempNavFile)) {
    return false;
  }
  if (!readItemContentsToStream(tocNavItem, tempNavFile, 1024)) {
    tempNavFile.close();
    return false;
  }
  tempNavFile.close();
  if (!SdMan.openFileForRead("EBP", tmpNavPath, tempNavFile)) {
    return false;
  }
  const auto navSize = tempNavFile.size();

  // Note: We can't use `contentBasePath` here as the nav file may be in a different folder to the content.opf
  // and the HTMLX nav file will have hrefs relative to itself
  const std::string navContentBasePath = tocNavItem.substr(0, tocNavItem.find_last_of('/') + 1);
  TocNavParser navParser(navContentBasePath, navSize, bookMetadataCache.get());

  if (!navParser.setup()) {
    LOG_ERR(TAG, "Could not setup toc nav parser");
    tempNavFile.close();
    return false;
  }

  const auto navBuffer = static_cast<uint8_t*>(malloc(1024));
  if (!navBuffer) {
    LOG_ERR(TAG, "Could not allocate memory for toc nav parser");
    tempNavFile.close();
    return false;
  }

  while (tempNavFile.available()) {
    const auto readSize = tempNavFile.read(navBuffer, 1024);
    const auto processedSize = navParser.write(navBuffer, readSize);

    if (processedSize != readSize) {
      LOG_ERR(TAG, "Could not process all toc nav data");
      free(navBuffer);
      tempNavFile.close();
      return false;
    }
  }

  free(navBuffer);
  tempNavFile.close();
  SdMan.remove(tmpNavPath.c_str());

  LOG_INF(TAG, "Parsed TOC nav items");
  return true;
}

bool Epub::parseCssFiles() {
  if (cssFiles_.empty()) {
    LOG_DBG(TAG, "No CSS files to parse");
    return true;
  }

  cssParser_.reset(new CssParser());

  for (const auto& cssHref : cssFiles_) {
    // Extract CSS file to temp location
    const auto tmpCssPath = getCachePath() + "/.tmp_css.css";

    FsFile tempCssFile;
    if (!SdMan.openFileForWrite("EBP", tmpCssPath, tempCssFile)) {
      LOG_ERR(TAG, "Failed to create temp CSS file");
      continue;
    }

    if (!readItemContentsToStream(cssHref, tempCssFile, 1024)) {
      LOG_ERR(TAG, "Failed to extract CSS: %s", cssHref.c_str());
      tempCssFile.close();
      SdMan.remove(tmpCssPath.c_str());
      continue;
    }
    tempCssFile.close();

    // Parse the CSS file
    if (!cssParser_->parseFile(tmpCssPath.c_str())) {
      LOG_ERR(TAG, "Failed to parse CSS: %s", cssHref.c_str());
    }

    // Clean up temp file
    SdMan.remove(tmpCssPath.c_str());
  }

  LOG_INF(TAG, "Parsed CSS files, %d style rules loaded", static_cast<int>(cssParser_->getStyleCount()));
  return true;
}

// load in the meta data for the epub file
bool Epub::load(const bool buildIfMissing) {
  LOG_INF(TAG, "Loading ePub: %s", filepath.c_str());

  // Initialize spine/TOC cache
  bookMetadataCache.reset(new BookMetadataCache(cachePath));

  // Try to load existing cache first
  if (bookMetadataCache->load()) {
    LOG_INF(TAG, "Loaded ePub: %s", filepath.c_str());
    return true;
  }

  // If we didn't load from cache above and we aren't allowed to build, fail now
  if (!buildIfMissing) {
    return false;
  }

  // Cache doesn't exist or is invalid, build it
  LOG_INF(TAG, "Cache not found, building spine/TOC cache");
  setupCacheDir();

  // Begin building cache - stream entries to disk immediately
  if (!bookMetadataCache->beginWrite()) {
    LOG_ERR(TAG, "Could not begin writing cache");
    return false;
  }

  // OPF Pass
  BookMetadataCache::BookMetadata bookMetadata;
  if (!bookMetadataCache->beginContentOpfPass()) {
    LOG_ERR(TAG, "Could not begin writing content.opf pass");
    return false;
  }
  if (!parseContentOpf(bookMetadata)) {
    LOG_ERR(TAG, "Could not parse content.opf");
    return false;
  }
  if (!bookMetadataCache->endContentOpfPass()) {
    LOG_ERR(TAG, "Could not end writing content.opf pass");
    return false;
  }

  // Parse CSS files for styling
  parseCssFiles();
  // Free CSS file paths - no longer needed after rules are parsed into cssParser_
  {
    std::vector<std::string> tmp;
    cssFiles_.swap(tmp);
  }

  // TOC Pass - try EPUB 3 nav first, fall back to NCX
  if (!bookMetadataCache->beginTocPass()) {
    LOG_ERR(TAG, "Could not begin writing toc pass");
    return false;
  }

  bool tocParsed = false;

  // Try EPUB 3 nav document first (preferred)
  if (!tocNavItem.empty()) {
    LOG_DBG(TAG, "Attempting to parse EPUB 3 nav document");
    tocParsed = parseTocNavFile();
  }

  // Fall back to NCX if nav parsing failed or wasn't available
  if (!tocParsed && !tocNcxItem.empty()) {
    LOG_DBG(TAG, "Falling back to NCX TOC");
    tocParsed = parseTocNcxFile();
  }

  if (!tocParsed) {
    LOG_ERR(TAG, "Warning: Could not parse any TOC format");
    // Continue anyway - book will work without TOC
  }

  if (!bookMetadataCache->endTocPass()) {
    LOG_ERR(TAG, "Could not end writing toc pass");
    return false;
  }

  // Close the cache files
  if (!bookMetadataCache->endWrite()) {
    LOG_ERR(TAG, "Could not end writing cache");
    return false;
  }

  // Build final book.bin
  if (!bookMetadataCache->buildBookBin(filepath, bookMetadata)) {
    LOG_ERR(TAG, "Could not update mappings and sizes");
    return false;
  }

  if (!bookMetadataCache->cleanupTmpFiles()) {
    LOG_ERR(TAG, "Could not cleanup tmp files - ignoring");
  }

  // Reload the cache from disk so it's in the correct state
  bookMetadataCache.reset(new BookMetadataCache(cachePath));
  if (!bookMetadataCache->load()) {
    LOG_ERR(TAG, "Failed to reload cache after writing");
    return false;
  }

  LOG_INF(TAG, "Loaded ePub: %s", filepath.c_str());
  return true;
}

bool Epub::clearCache() const {
  if (!SdMan.exists(cachePath.c_str())) {
    LOG_DBG(TAG, "Cache does not exist, no action needed");
    return true;
  }

  if (!SdMan.removeDir(cachePath.c_str())) {
    LOG_ERR(TAG, "Failed to clear cache");
    return false;
  }

  LOG_INF(TAG, "Cache cleared successfully");
  return true;
}

void Epub::setupCacheDir() const {
  if (!SdMan.exists(cachePath.c_str())) {
    SdMan.mkdir(cachePath.c_str());
  }

  // Create sections subdirectory for page cache
  const auto sectionsDir = cachePath + "/sections";
  if (!SdMan.exists(sectionsDir.c_str())) {
    SdMan.mkdir(sectionsDir.c_str());
  }

  // Create images subdirectory for cached images
  const auto imagesDir = cachePath + "/images";
  if (!SdMan.exists(imagesDir.c_str())) {
    SdMan.mkdir(imagesDir.c_str());
  }
}

const std::string& Epub::getCachePath() const { return cachePath; }

const std::string& Epub::getPath() const { return filepath; }

const std::string& Epub::getTitle() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.title;
}

const std::string& Epub::getAuthor() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.author;
}

const std::string& Epub::getLanguage() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.language;
}

std::string Epub::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

std::string Epub::getCoverPreviewBmpPath() const { return cachePath + "/cover_preview.bmp"; }

bool Epub::generateCoverPreviewBmp() const {
  const auto previewPath = getCoverPreviewBmpPath();

  // Already generated, return true
  if (SdMan.exists(previewPath.c_str())) {
    return true;
  }

  // Full cover already exists, no need for preview
  if (SdMan.exists(getCoverBmpPath().c_str())) {
    return false;
  }

  setupCacheDir();

  // Priority 1: External cover file (bookname.jpg, etc.)
  std::string externalCover = findCoverImage();
  if (!externalCover.empty()) {
    LOG_DBG(TAG, "Found external cover for preview: %s", externalCover.c_str());
    ImageConvertConfig config;
    config.quickMode = true;
    config.logTag = "EBP";
    if (ImageConverterFactory::convertToBmp(externalCover, previewPath, config)) {
      LOG_INF(TAG, "Generated cover preview from external image");
      return true;
    }
    // Conversion failed - clean up any partial output
    SdMan.remove(previewPath.c_str());
  }

  // Priority 2: Try common internal cover paths (batch scan - single ZIP pass)
  static const char* const commonCoverPaths[] = {
      "cover.jpg",
      "cover.jpeg",
      "cover.png",
      "images/cover.jpg",
      "images/cover.jpeg",
      "images/cover.png",
      "Images/cover.jpg",
      "Images/cover.jpeg",
      "Images/cover.png",
      "OEBPS/cover.jpg",
      "OEBPS/cover.jpeg",
      "OEBPS/cover.png",
      "OEBPS/images/cover.jpg",
      "OEBPS/images/cover.jpeg",
      "OEBPS/images/cover.png",
      "OEBPS/Images/cover.jpg",
      "OEBPS/Images/cover.jpeg",
      "OEBPS/Images/cover.png",
  };
  constexpr int commonCoverPathsCount = sizeof(commonCoverPaths) / sizeof(commonCoverPaths[0]);

  ZipFile zip(filepath);
  const int foundIndex = zip.findFirstExisting(commonCoverPaths, commonCoverPathsCount);
  if (foundIndex >= 0) {
    const char* path = commonCoverPaths[foundIndex];
    LOG_DBG(TAG, "Found cover for preview via heuristic: %s", path);

    const std::string ext = FsHelpers::isJpegFile(path) ? ".jpg" : ".png";
    const auto coverTempPath = getCachePath() + "/.cover_preview" + ext;

    FsFile coverFile;
    if (SdMan.openFileForWrite("EBP", coverTempPath, coverFile)) {
      if (readItemContentsToStream(path, coverFile, 1024)) {
        coverFile.close();
        ImageConvertConfig config;
        config.quickMode = true;
        config.logTag = "EBP";
        if (ImageConverterFactory::convertToBmp(coverTempPath, previewPath, config)) {
          SdMan.remove(coverTempPath.c_str());
          return true;
        }
        // Conversion failed - clean up partial output
        SdMan.remove(previewPath.c_str());
      } else {
        coverFile.close();
      }
    }
    SdMan.remove(coverTempPath.c_str());
  }

  LOG_DBG(TAG, "No cover found for preview");
  return false;
}

std::string Epub::getThumbBmpPath() const { return cachePath + "/thumb.bmp"; }

std::string Epub::findCoverImage() const {
  size_t lastSlash = filepath.find_last_of('/');
  std::string dirPath = (lastSlash == std::string::npos) ? "/" : filepath.substr(0, lastSlash);
  if (dirPath.empty()) dirPath = "/";

  std::string baseName = filepath.substr(lastSlash + 1);
  size_t lastDot = baseName.find_last_of('.');
  if (lastDot != std::string::npos) {
    baseName = baseName.substr(0, lastDot);
  }

  return CoverHelpers::findCoverImage(dirPath, baseName);
}

bool Epub::generateThumbBmp() const {
  const auto thumbPath = getThumbBmpPath();
  const auto failedMarkerPath = cachePath + "/.thumb.failed";

  // Already generated, return true
  if (SdMan.exists(thumbPath.c_str())) {
    return true;
  }

  // Previously failed, don't retry
  if (SdMan.exists(failedMarkerPath.c_str())) {
    return false;
  }

  setupCacheDir();

  // Priority 1: External cover file (bookname.jpg, etc.)
  std::string externalCover = findCoverImage();
  if (!externalCover.empty()) {
    LOG_DBG(TAG, "Found external cover: %s", externalCover.c_str());
    // Generate full-size cover BMP first (needed for thumbnail scaling)
    const auto coverPath = getCoverBmpPath();
    if (!SdMan.exists(coverPath.c_str())) {
      if (CoverHelpers::convertImageToBmp(externalCover, coverPath, "EBP", true)) {
        LOG_INF(TAG, "Generated cover BMP from external image");
      }
    }
    // Now generate thumbnail from cover
    if (CoverHelpers::generateThumbFromCover(coverPath, thumbPath, "EBP")) {
      return true;
    }
  }

  // Priority 2: Try to generate from existing cover.bmp (much faster than re-extracting from EPUB)
  const auto coverPath = getCoverBmpPath();
  if (CoverHelpers::generateThumbFromCover(coverPath, thumbPath, "EBP")) {
    return true;
  }

  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR(TAG, "Cannot generate thumb BMP, cache not loaded");
    return false;
  }

  const auto coverImageHref = bookMetadataCache->coreMetadata.coverItemHref;
  if (coverImageHref.empty()) {
    LOG_DBG(TAG, "No known cover image for thumbnail");
    // Create failure marker so we don't retry
    FsFile marker;
    if (SdMan.openFileForWrite("EBP", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  // Check if format is supported
  if (!ImageConverterFactory::isSupported(coverImageHref)) {
    LOG_ERR(TAG, "Unsupported cover image format for thumbnail");
    // Create failure marker so we don't retry
    FsFile marker;
    if (SdMan.openFileForWrite("EBP", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  // Extract cover image to temp file, then convert to thumbnail
  LOG_DBG(TAG, "Generating thumb BMP from cover image");
  const std::string ext = FsHelpers::isJpegFile(coverImageHref) ? ".jpg" : ".png";
  const auto coverTempPath = getCachePath() + "/.cover" + ext;
  const auto thumbTempPath = thumbPath + ".tmp";

  FsFile coverFile;
  if (!SdMan.openFileForWrite("EBP", coverTempPath, coverFile)) {
    return false;
  }
  if (!readItemContentsToStream(coverImageHref, coverFile, 1024)) {
    coverFile.close();
    return false;
  }
  coverFile.close();

  // Use 1-bit dithering for JPEG thumbnails (smaller files). PNG thumbnails are
  // always 2-bit since PngToBmpConverter doesn't support 1-bit output.
  ImageConvertConfig config;
  config.maxWidth = THUMB_WIDTH;
  config.maxHeight = THUMB_HEIGHT;
  config.oneBit = FsHelpers::isJpegFile(coverImageHref);
  config.logTag = "EBP";

  const bool success = ImageConverterFactory::convertToBmp(coverTempPath, thumbTempPath, config);
  SdMan.remove(coverTempPath.c_str());

  if (success) {
    // Atomic rename: readers see either no file or complete file
    FsFile tempFile = SdMan.open(thumbTempPath.c_str(), O_RDWR);
    if (tempFile) {
      tempFile.rename(thumbPath.c_str());
      tempFile.close();
    }
  } else {
    LOG_ERR(TAG, "Failed to generate thumb BMP from cover image");
    SdMan.remove(thumbTempPath.c_str());
    // Create failure marker so we don't retry
    FsFile marker;
    if (SdMan.openFileForWrite("EBP", failedMarkerPath, marker)) {
      marker.close();
    }
  }
  LOG_INF(TAG, "Generated thumb BMP from cover image, success: %s", success ? "yes" : "no");
  return success;
}

bool Epub::generateCoverBmp(bool use1BitDithering) const {
  const auto coverPath = getCoverBmpPath();
  const auto failedMarkerPath = cachePath + "/.cover.failed";

  // Already generated, return true
  if (SdMan.exists(coverPath.c_str())) {
    return true;
  }

  // Previously failed, don't retry
  if (SdMan.exists(failedMarkerPath.c_str())) {
    return false;
  }

  setupCacheDir();

  // Priority 1: External cover file (bookname.jpg, etc.)
  std::string externalCover = findCoverImage();
  if (!externalCover.empty()) {
    LOG_DBG(TAG, "Found external cover: %s", externalCover.c_str());
    if (CoverHelpers::convertImageToBmp(externalCover, coverPath, "EBP", use1BitDithering)) {
      LOG_INF(TAG, "Generated cover BMP from external image");
      return true;
    }
  }

  // Priority 1.5: Try common internal cover paths (batch scan - single ZIP pass)
  static const char* const commonCoverPaths[] = {
      // Root level
      "cover.jpg",
      "cover.jpeg",
      "cover.png",
      // images/ directory (lowercase - most common)
      "images/cover.jpg",
      "images/cover.jpeg",
      "images/cover.png",
      // Images/ directory (capitalized - common in Calibre)
      "Images/cover.jpg",
      "Images/cover.jpeg",
      "Images/cover.png",
      // OEBPS/ root
      "OEBPS/cover.jpg",
      "OEBPS/cover.jpeg",
      "OEBPS/cover.png",
      // OEBPS/images/
      "OEBPS/images/cover.jpg",
      "OEBPS/images/cover.jpeg",
      "OEBPS/images/cover.png",
      // OEBPS/Images/
      "OEBPS/Images/cover.jpg",
      "OEBPS/Images/cover.jpeg",
      "OEBPS/Images/cover.png",
      // OPS/ variants (older EPUB 2)
      "OPS/cover.jpg",
      "OPS/cover.jpeg",
      "OPS/cover.png",
      "OPS/images/cover.jpg",
      "OPS/images/cover.jpeg",
      "OPS/images/cover.png",
      "OPS/Images/cover.jpg",
      "OPS/Images/cover.jpeg",
      "OPS/Images/cover.png",
      // EPUB/ variants (EPUB 3)
      "EPUB/cover.jpg",
      "EPUB/cover.jpeg",
      "EPUB/cover.png",
      "EPUB/images/cover.jpg",
      "EPUB/images/cover.jpeg",
      "EPUB/images/cover.png",
      // EPUB/Images/ (capitalized, for consistency with OEBPS/Images/)
      "EPUB/Images/cover.jpg",
      "EPUB/Images/cover.jpeg",
      "EPUB/Images/cover.png",
  };
  constexpr int commonCoverPathsCount = sizeof(commonCoverPaths) / sizeof(commonCoverPaths[0]);

  // Use single ZipFile instance with batch lookup for efficiency
  ZipFile zip(filepath);
  const int foundIndex = zip.findFirstExisting(commonCoverPaths, commonCoverPathsCount);
  if (foundIndex >= 0) {
    const char* path = commonCoverPaths[foundIndex];
    // Note: No file size check needed - converters have built-in limits:
    // - JPEG: MAX_MCU_ROW_BYTES=64KB limits width to ~4K-8K pixels
    // - PNG: MAX_IMAGE_WIDTH=2048, MAX_IMAGE_HEIGHT=3072
    LOG_DBG(TAG, "Found cover via heuristic: %s", path);

    const std::string ext = FsHelpers::isJpegFile(path) ? ".jpg" : ".png";
    const auto coverTempPath = getCachePath() + "/.cover" + ext;

    FsFile coverFile;
    if (SdMan.openFileForWrite("EBP", coverTempPath, coverFile)) {
      if (readItemContentsToStream(path, coverFile, 1024)) {
        coverFile.close();
        ImageConvertConfig config;
        config.oneBit = use1BitDithering;
        config.logTag = "EBP";
        if (ImageConverterFactory::convertToBmp(coverTempPath, coverPath, config)) {
          SdMan.remove(coverTempPath.c_str());
          return true;
        }
        // Conversion failed - clean up partial output
        SdMan.remove(coverPath.c_str());
      } else {
        coverFile.close();
      }
    }
    SdMan.remove(coverTempPath.c_str());
  }

  // Priority 2: Internal EPUB cover via OPF metadata
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR(TAG, "Cannot generate cover BMP, cache not loaded");
    return false;
  }

  const auto coverImageHref = bookMetadataCache->coreMetadata.coverItemHref;
  if (coverImageHref.empty()) {
    LOG_DBG(TAG, "No known cover image");
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("EBP", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  // Check if format is supported
  if (!ImageConverterFactory::isSupported(coverImageHref)) {
    LOG_ERR(TAG, "Unsupported cover image format");
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("EBP", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  // Extract cover image to temp file, then convert
  LOG_DBG(TAG, "Generating BMP from cover image");
  const std::string ext = FsHelpers::isJpegFile(coverImageHref) ? ".jpg" : ".png";
  const auto coverTempPath = getCachePath() + "/.cover" + ext;

  FsFile coverFile;
  if (!SdMan.openFileForWrite("EBP", coverTempPath, coverFile)) {
    return false;
  }
  if (!readItemContentsToStream(coverImageHref, coverFile, 1024)) {
    coverFile.close();
    return false;
  }
  coverFile.close();

  ImageConvertConfig config;
  config.oneBit = use1BitDithering;
  config.logTag = "EBP";

  const bool success = ImageConverterFactory::convertToBmp(coverTempPath, coverPath, config);
  SdMan.remove(coverTempPath.c_str());

  if (!success) {
    LOG_ERR(TAG, "Failed to generate BMP from cover image");
    SdMan.remove(coverPath.c_str());
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("EBP", failedMarkerPath, marker)) {
      marker.close();
    }
  }
  LOG_INF(TAG, "Generated BMP from cover image, success: %s", success ? "yes" : "no");
  return success;
}

uint8_t* Epub::readItemContentsToBytes(const std::string& itemHref, size_t* size, const bool trailingNullByte) const {
  if (itemHref.empty()) {
    LOG_ERR(TAG, "Failed to read item, empty href");
    return nullptr;
  }

  const std::string path = FsHelpers::normalisePath(itemHref);

  const auto content = ZipFile(filepath).readFileToMemory(path.c_str(), size, trailingNullByte);
  if (!content) {
    LOG_ERR(TAG, "Failed to read item %s", path.c_str());
    return nullptr;
  }

  return content;
}

bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize,
                                    uint8_t* dictBuffer) const {
  if (itemHref.empty()) {
    LOG_ERR(TAG, "Failed to read item, empty href");
    return false;
  }

  const std::string path = FsHelpers::normalisePath(itemHref);
  return ZipFile(filepath).readFileToStream(path.c_str(), out, chunkSize, dictBuffer);
}

bool Epub::getItemSize(const std::string& itemHref, size_t* size) const {
  const std::string path = FsHelpers::normalisePath(itemHref);
  return ZipFile(filepath).getInflatedFileSize(path.c_str(), size);
}

int Epub::getSpineItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }
  return bookMetadataCache->getSpineCount();
}

BookMetadataCache::SpineEntry Epub::getSpineItem(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR(TAG, "getSpineItem called but cache not loaded");
    return {};
  }

  if (spineIndex < 0 || spineIndex >= bookMetadataCache->getSpineCount()) {
    LOG_ERR(TAG, "getSpineItem index:%d is out of range", spineIndex);
    return bookMetadataCache->getSpineEntry(0);
  }

  return bookMetadataCache->getSpineEntry(spineIndex);
}

BookMetadataCache::TocEntry Epub::getTocItem(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR(TAG, "getTocItem called but cache not loaded");
    return {};
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_ERR(TAG, "getTocItem index:%d is out of range", tocIndex);
    return {};
  }

  return bookMetadataCache->getTocEntry(tocIndex);
}

int Epub::getTocItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }

  return bookMetadataCache->getTocCount();
}

// work out the section index for a toc index
int Epub::getSpineIndexForTocIndex(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR(TAG, "getSpineIndexForTocIndex called but cache not loaded");
    return 0;
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_ERR(TAG, "getSpineIndexForTocIndex: tocIndex %d out of range", tocIndex);
    return 0;
  }

  const int spineIndex = bookMetadataCache->getTocEntry(tocIndex).spineIndex;
  if (spineIndex < 0) {
    LOG_ERR(TAG, "Section not found for TOC index %d", tocIndex);
    return 0;
  }

  return spineIndex;
}

int Epub::getTocIndexForSpineIndex(const int spineIndex) const { return getSpineItem(spineIndex).tocIndex; }

int Epub::getSpineIndexForTextReference() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR(TAG, "getSpineIndexForTextReference called but cache not loaded");
    return 0;
  }
  LOG_DBG(TAG, "Core Metadata: cover(%d)=%s, textReference(%d)=%s",
          bookMetadataCache->coreMetadata.coverItemHref.size(), bookMetadataCache->coreMetadata.coverItemHref.c_str(),
          bookMetadataCache->coreMetadata.textReferenceHref.size(),
          bookMetadataCache->coreMetadata.textReferenceHref.c_str());

  if (bookMetadataCache->coreMetadata.textReferenceHref.empty()) {
    // there was no textReference in epub, so we return 0 (the first chapter)
    return 0;
  }

  // loop through spine items to get the correct index matching the text href
  for (size_t i = 0; i < getSpineItemsCount(); i++) {
    if (getSpineItem(i).href == bookMetadataCache->coreMetadata.textReferenceHref) {
      LOG_DBG(TAG, "Text reference %s found at index %d", bookMetadataCache->coreMetadata.textReferenceHref.c_str(), i);
      return i;
    }
  }
  // This should not happen, as we checked for empty textReferenceHref earlier
  LOG_ERR(TAG, "Section not found for text reference");
  return 0;
}
