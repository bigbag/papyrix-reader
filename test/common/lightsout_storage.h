#pragma once

#include <SdFat.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "hal/Storage.h"

namespace lightsout_test {
constexpr const char* finalPath = "/.papyrix/apps/lightsout.bin";
constexpr const char* backupPath = "/.papyrix/apps/lightsout.bin.bak";
constexpr const char* tempPath = "/.papyrix/apps/lightsout.bin.tmp";
inline std::map<std::string, std::shared_ptr<std::string>> files;
enum class Failure { None, Write, Sync, Close, Preserve, Publish, Rollback, Read, Clear, PreserveAfter, PublishAfter };
inline Failure failure = Failure::None;
inline void put(const char* path, const std::string& value) { files[path] = std::make_shared<std::string>(value); }
inline void reset() {
  files.clear();
  failure = Failure::None;
}
}  // namespace lightsout_test

namespace papyrix::hal {
Result<bool> Storage::exists(const char* path) { return Ok(lightsout_test::files.count(path) != 0); }
Result<void> Storage::mkdir(const char*) { return Ok(); }
Result<void> Storage::openWrite(const char* path, FsFile& out) {
  using namespace lightsout_test;
  put(path, "");
  out.setSharedBuffer(files.at(path));
  if (failure == Failure::Write) out.setWriteLimit(7);
  if (failure == Failure::Sync) out.setSyncResult(false);
  if (failure == Failure::Close) out.setCloseResult(false);
  return Ok();
}
Result<void> Storage::openRead(const char* path, FsFile& out) {
  using namespace lightsout_test;
  if (!files.count(path) || failure == Failure::Read) return ErrVoid(Error::IOError);
  out.setBuffer(*files.at(path));
  return Ok();
}
Result<void> Storage::remove(const char* path) {
  using namespace lightsout_test;
  if (failure == Failure::Clear) return ErrVoid(Error::IOError);
  return files.erase(path) ? Ok() : ErrVoid(Error::FileNotFound);
}
Result<void> Storage::rename(const char* from, const char* to) {
  using namespace lightsout_test;
  if (!files.count(from) || files.count(to)) return ErrVoid(Error::IOError);
  if ((failure == Failure::Preserve && std::strcmp(from, finalPath) == 0) ||
      ((failure == Failure::Publish || failure == Failure::Rollback) && std::strcmp(from, tempPath) == 0) ||
      (failure == Failure::Rollback && std::strcmp(from, backupPath) == 0))
    return ErrVoid(Error::IOError);
  files[to] = files.at(from);
  files.erase(from);
  if ((failure == Failure::PreserveAfter && std::strcmp(from, finalPath) == 0) ||
      (failure == Failure::PublishAfter && std::strcmp(from, tempPath) == 0))
    return ErrVoid(Error::IOError);
  return Ok();
}
}  // namespace papyrix::hal
