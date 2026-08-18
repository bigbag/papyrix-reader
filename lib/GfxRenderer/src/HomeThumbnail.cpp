#include "HomeThumbnail.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Logging.h>
#include <PackedCoverReducer.h>
#include <SDCardManager.h>

#include <algorithm>

namespace home_thumbnail {

void fitDimensions(const int sourceWidth, const int sourceHeight, int& destinationWidth, int& destinationHeight) {
  destinationWidth = sourceWidth;
  destinationHeight = sourceHeight;
  if (sourceWidth <= MAX_WIDTH && sourceHeight <= MAX_HEIGHT) return;

  if (static_cast<uint64_t>(sourceWidth) * MAX_HEIGHT > static_cast<uint64_t>(sourceHeight) * MAX_WIDTH) {
    destinationWidth = MAX_WIDTH;
    destinationHeight = std::max(1, static_cast<int>(static_cast<uint64_t>(sourceHeight) * MAX_WIDTH / sourceWidth));
  } else {
    destinationHeight = MAX_HEIGHT;
    destinationWidth = std::max(1, static_cast<int>(static_cast<uint64_t>(sourceWidth) * MAX_HEIGHT / sourceHeight));
  }
}

namespace {

bool isAbortRequested(const std::function<bool()>& shouldAbort) { return shouldAbort && shouldAbort(); }

std::string parentPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string markerPathFor(const std::string& thumbnailPath) {
  const std::string parent = parentPath(thumbnailPath);
  return parent.empty() ? ".thumb.failed" : parent + "/.thumb.failed";
}

void createMarker(const std::string& path) {
  FsFile marker;
  if (SdMan.openFileForWrite("THUMB", path, marker)) marker.close();
}

void removeTemporaryFiles(const std::string& tempPath, const std::string& partPath) {
  SdMan.remove(tempPath.c_str());
  SdMan.remove(partPath.c_str());
}

Result fail(const std::string& markerPath, const std::string& tempPath, const std::string& partPath,
            const std::function<bool()>& shouldAbort) {
  removeTemporaryFiles(tempPath, partPath);
  if (isAbortRequested(shouldAbort)) return Result::Cancelled;
  createMarker(markerPath);
  return Result::Unavailable;
}

bool validateBmp(const std::string& path, const int maxWidth, const int maxHeight, Info* info) {
  FsFile file;
  if (!SdMan.openFileForRead("THUMB", path, file)) return false;

  Bitmap bitmap(file);
  const bool valid = bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.hasCompletePixelData() &&
                     bitmap.getBpp() == 1 && bitmap.isTopDown() && (maxWidth <= 0 || bitmap.getWidth() <= maxWidth) &&
                     (maxHeight <= 0 || bitmap.getHeight() <= maxHeight);
  if (valid && info) {
    info->width = bitmap.getWidth();
    info->height = bitmap.getHeight();
  }
  file.close();
  return valid;
}

bool copyFile(FsFile& source, FsFile& output, const std::function<bool()>& shouldAbort) {
  if (!source.seek(0)) return false;

  uint8_t buffer[128];
  while (source.available() > 0) {
    if (isAbortRequested(shouldAbort)) return false;
    // read() reports I/O errors as -1; a size_t would make that SIZE_MAX and
    // pass it to write() as a length.
    const int bytesRead = source.read(buffer, sizeof(buffer));
    if (bytesRead <= 0) return false;
    const size_t chunk = static_cast<size_t>(bytesRead);
    if (output.write(buffer, chunk) != chunk) return false;
  }
  return true;
}

}  // namespace

std::string pathForCache(const std::string& cachePath) { return cachePath + "/" + FILE_NAME; }

std::string coverPathForCache(const std::string& cachePath) { return cachePath + "/" + COVER_FILE_NAME; }

bool validate(const std::string& path, Info* info) { return validateBmp(path, MAX_WIDTH, MAX_HEIGHT, info); }

bool validateCover(const std::string& path, Info* info) { return validateBmp(path, 0, 0, info); }

HomeImageSelection selectForHome(const bool imagesEnabled, const std::string& thumbnailPath,
                                 const std::string& coverPath) {
  if (!imagesEnabled) return {};
  if (validate(thumbnailPath)) return {HomeImageType::Thumbnail, thumbnailPath};
  if (validateCover(coverPath)) return {HomeImageType::Cover, coverPath};
  return {};
}

Result generateFromCover(const std::string& coverPath, const std::string& thumbnailPath,
                         const std::function<bool()>& shouldAbort) {
  const std::string markerPath = markerPathFor(thumbnailPath);
  const std::string tempPath = thumbnailPath + ".tmp";
  const std::string partPath = tempPath + ".part";

  if (validate(thumbnailPath)) {
    SdMan.remove(markerPath.c_str());
    return Result::Ready;
  }
  if (SdMan.exists(markerPath.c_str())) return Result::Unavailable;
  if (isAbortRequested(shouldAbort)) return Result::Cancelled;

  removeTemporaryFiles(tempPath, partPath);

  FsFile sourceFile;
  if (!SdMan.openFileForRead("THUMB", coverPath, sourceFile)) {
    return fail(markerPath, tempPath, partPath, shouldAbort);
  }

  Bitmap bitmap(sourceFile);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || !bitmap.hasCompletePixelData() || bitmap.getBpp() != 1 ||
      !bitmap.isTopDown()) {
    sourceFile.close();
    return fail(markerPath, tempPath, partPath, shouldAbort);
  }

  FsFile outputFile;
  if (!SdMan.openFileForWrite("THUMB", partPath, outputFile)) {
    sourceFile.close();
    return fail(markerPath, tempPath, partPath, shouldAbort);
  }

  int destinationWidth;
  int destinationHeight;
  fitDimensions(bitmap.getWidth(), bitmap.getHeight(), destinationWidth, destinationHeight);
  const bool fits = destinationWidth == bitmap.getWidth() && destinationHeight == bitmap.getHeight();
  bool success = false;
  ReduceResult reduceResult = ReduceResult::Ready;
  if (fits) {
    success = copyFile(sourceFile, outputFile, shouldAbort);
  } else {
    LOG_INF("THUMB", "Reducing cover %dx%d -> %dx%d", bitmap.getWidth(), bitmap.getHeight(), destinationWidth,
            destinationHeight);
    const unsigned long started = millis();
    reduceResult = reducePackedCover(bitmap, outputFile, destinationWidth, destinationHeight, shouldAbort);
    const unsigned long elapsed = millis() - started;
    if (reduceResult == ReduceResult::Ready) {
      LOG_INF("THUMB", "Thumbnail reduction completed in %lu ms", elapsed);
    } else if (reduceResult == ReduceResult::Cancelled) {
      LOG_INF("THUMB", "Thumbnail reduction cancelled after %lu ms", elapsed);
    } else {
      LOG_ERR("THUMB", "Thumbnail reduction failed after %lu ms", elapsed);
    }
    success = reduceResult == ReduceResult::Ready;
  }

  outputFile.close();
  sourceFile.close();

  if (reduceResult == ReduceResult::Cancelled) {
    removeTemporaryFiles(tempPath, partPath);
    return Result::Cancelled;
  }
  if (!success || isAbortRequested(shouldAbort)) {
    return fail(markerPath, tempPath, partPath, shouldAbort);
  }
  if (!validate(partPath) || !SdMan.commitFile(partPath.c_str(), tempPath.c_str()) || !validate(tempPath)) {
    return fail(markerPath, tempPath, partPath, shouldAbort);
  }
  if (isAbortRequested(shouldAbort)) {
    removeTemporaryFiles(tempPath, partPath);
    return Result::Cancelled;
  }
  if (!SdMan.commitFile(tempPath.c_str(), thumbnailPath.c_str())) {
    return fail(markerPath, tempPath, partPath, shouldAbort);
  }
  if (!validate(thumbnailPath)) {
    SdMan.remove(thumbnailPath.c_str());
    return fail(markerPath, tempPath, partPath, shouldAbort);
  }
  if (isAbortRequested(shouldAbort)) {
    SdMan.remove(thumbnailPath.c_str());
    removeTemporaryFiles(tempPath, partPath);
    return Result::Cancelled;
  }

  SdMan.remove(markerPath.c_str());
  LOG_INF("THUMB", "Published thumbnail: %s", thumbnailPath.c_str());
  return Result::Ready;
}

}  // namespace home_thumbnail
