#include "Storage.h"

#include <SDCardManager.h>

namespace papyrix {
namespace drivers {

Result<void> Storage::init() {
  if (mounted_) {
    return Ok();
  }

  if (!SdMan.begin()) {
    return ErrVoid(Error::SdCardNotFound);
  }

  mounted_ = true;
  return Ok();
}

void Storage::shutdown() {
  // SdFat doesn't have an explicit shutdown
  mounted_ = false;
}

Result<void> Storage::openRead(const char* path, FsFile& out) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.openFileForRead("DRV", path, out)) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<void> Storage::openWrite(const char* path, FsFile& out) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.openFileForWrite("DRV", path, out)) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<bool> Storage::exists(const char* path) {
  if (!mounted_) {
    return Err<bool>(Error::SdCardNotFound);
  }

  return Ok(SdMan.exists(path));
}

Result<bool> Storage::isDirectory(const char* path) {
  if (!mounted_) {
    return Err<bool>(Error::SdCardNotFound);
  }
  if (!path || path[0] == '\0') {
    return Err<bool>(Error::InvalidOperation);
  }

  FsFile entry = SdMan.open(path);
  if (!entry) {
    return Err<bool>(Error::FileNotFound);
  }
  const bool directory = entry.isDirectory();
  entry.close();
  return Ok(directory);
}

Result<void> Storage::remove(const char* path) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.remove(path)) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<void> Storage::rename(const char* oldPath, const char* newPath) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }
  if (!oldPath || !newPath || oldPath[0] == '\0' || newPath[0] == '\0') {
    return ErrVoid(Error::InvalidOperation);
  }
  if (!SdMan.rename(oldPath, newPath)) {
    return ErrVoid(Error::IOError);
  }
  return Ok();
}

Result<void> Storage::commitFile(const char* tmpPath, const char* finalPath) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }
  if (!tmpPath || !finalPath || tmpPath[0] == '\0' || finalPath[0] == '\0') {
    return ErrVoid(Error::InvalidOperation);
  }
  if (!SdMan.commitFile(tmpPath, finalPath)) {
    return ErrVoid(Error::IOError);
  }
  return Ok();
}

Result<void> Storage::mkdir(const char* path) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.mkdir(path)) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<void> Storage::rmdir(const char* path, RemoveDirProgress progress) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.removeDir(path, std::move(progress))) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<void> Storage::openDir(const char* path, FsFile& out) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  out = SdMan.open(path);
  if (!out) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<size_t> Storage::readToBuffer(const char* path, char* buffer, size_t bufferSize) {
  if (!mounted_) {
    return Err<size_t>(Error::SdCardNotFound);
  }

  size_t bytesRead = SdMan.readFileToBuffer(path, buffer, bufferSize);
  if (bytesRead == 0) {
    return Err<size_t>(Error::FileNotFound);
  }

  return Ok(bytesRead);
}

}  // namespace drivers
}  // namespace papyrix
