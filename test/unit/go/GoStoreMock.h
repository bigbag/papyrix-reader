#pragma once

#include <SdFat.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "hal/Storage.h"

namespace go_store_test {
constexpr const char* finalPath = "/.papyrix/apps/go.bin";
constexpr const char* tempPath = "/.papyrix/apps/go.bin.tmp";
constexpr const char* backupPath = "/.papyrix/apps/go.bin.bak";
inline std::map<std::string, std::shared_ptr<std::string>> files;
enum class Failure {
  None,
  Read,
  HistoryRead,
  Exists,
  Write,
  Sync,
  Close,
  Preserve,
  Publish,
  Rollback,
  PreserveAfter,
  PublishAfter,
  Remove,
  Recovery,
  RecoveryAfter
};
inline Failure failure = Failure::None;
inline unsigned reads = 0;
inline void put(const char* path, const std::string& data) { files[path] = std::make_shared<std::string>(data); }
inline void reset() {
  files.clear();
  failure = Failure::None;
  reads = 0;
}
}  // namespace go_store_test

namespace papyrix::hal {
Result<bool> Storage::exists(const char* path) {
  if (go_store_test::failure == go_store_test::Failure::Exists) return Err<bool>(Error::IOError);
  return Ok(go_store_test::files.count(path) != 0);
}
Result<void> Storage::mkdir(const char*) { return Ok(); }
Result<void> Storage::openRead(const char* path, FsFile& out) {
  using namespace go_store_test;
  ++reads;
  if (failure == Failure::Read || !files.count(path)) return ErrVoid(Error::IOError);
  out.setBuffer(*files.at(path));
  if (failure == Failure::HistoryRead && reads == 2) out.setReadLimit(0);
  return Ok();
}
Result<void> Storage::openWrite(const char* path, FsFile& out) {
  using namespace go_store_test;
  put(path, "");
  out.setSharedBuffer(files.at(path));
  if (failure == Failure::Write) out.setWriteLimit(12);
  if (failure == Failure::Sync) out.setSyncResult(false);
  if (failure == Failure::Close) out.setCloseResult(false);
  return Ok();
}
Result<void> Storage::remove(const char* path) {
  using namespace go_store_test;
  if (failure == Failure::Remove) return ErrVoid(Error::IOError);
  return files.erase(path) ? Ok() : ErrVoid(Error::FileNotFound);
}
Result<void> Storage::rename(const char* from, const char* to) {
  using namespace go_store_test;
  if (!files.count(from) || files.count(to)) return ErrVoid(Error::IOError);
  if ((failure == Failure::Preserve && std::strcmp(from, finalPath) == 0) ||
      ((failure == Failure::Publish || failure == Failure::Rollback) && std::strcmp(from, tempPath) == 0) ||
      ((failure == Failure::Rollback || failure == Failure::Recovery) && std::strcmp(from, backupPath) == 0))
    return ErrVoid(Error::IOError);
  files[to] = files.at(from);
  files.erase(from);
  if ((failure == Failure::PreserveAfter && std::strcmp(from, finalPath) == 0) ||
      (failure == Failure::PublishAfter && std::strcmp(from, tempPath) == 0) ||
      (failure == Failure::RecoveryAfter && std::strcmp(from, backupPath) == 0))
    return ErrVoid(Error::IOError);
  return Ok();
}
}  // namespace papyrix::hal
