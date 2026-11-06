#include <SdFat.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "apps/ClockSettings.h"
#include "hal/Storage.h"
#include "test_utils.h"

namespace {
constexpr const char* finalPath = "/.papyrix/apps/clock.txt";
constexpr const char* backupPath = "/.papyrix/apps/clock.txt.bak";
constexpr const char* tempPath = "/.papyrix/apps/clock.txt.tmp";
std::map<std::string, std::shared_ptr<std::string>> files;
enum class Failure { None, Write, Sync, Close, Preserve, Publish, Rollback, Read, PreserveAfter, PublishAfter };
Failure failure = Failure::None;
void put(const char* path, const std::string& value) { files[path] = std::make_shared<std::string>(value); }
}  // namespace

namespace papyrix::hal {
Result<bool> Storage::exists(const char* path) { return Ok(files.count(path) != 0); }
Result<void> Storage::mkdir(const char*) { return Ok(); }
Result<void> Storage::openWrite(const char* path, FsFile& out) {
  put(path, "");
  out.setSharedBuffer(files.at(path));
  if (failure == Failure::Write) out.setWriteLimit(12);
  if (failure == Failure::Sync) out.setSyncResult(false);
  if (failure == Failure::Close) out.setCloseResult(false);
  return Ok();
}
Result<void> Storage::openRead(const char* path, FsFile& out) {
  if (!files.count(path) || failure == Failure::Read) return ErrVoid(Error::IOError);
  out.setBuffer(*files.at(path));
  return Ok();
}
Result<void> Storage::remove(const char* path) { return files.erase(path) ? Ok() : ErrVoid(Error::FileNotFound); }
Result<void> Storage::rename(const char* from, const char* to) {
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

int main() {
  using namespace papyrix::clock_app;
  using papyrix::clock_faces::Face;
  TestUtils::TestRunner runner("Clock settings");
  const auto missing = decodeSettings(nullptr, 0, false);
  runner.expectEq(int(PowerMode::Normal), int(missing.powerMode), "mode_migration: missing file stays Normal");
  runner.expectEq(int(Face::Big), int(missing.face), "mode_migration: missing file uses Big");
  const char legacy[] = "utcOffset=7\nuse24h=0\ndateFmt=3\nntpSync=2\n";
  const auto old = decodeSettings(legacy, sizeof(legacy) - 1, true);
  runner.expectEq(int(Face::Retro), int(old.face), "mode_migration: existing file without face uses Retro");
  runner.expectEq(int(PowerMode::Normal), int(old.powerMode), "mode_migration: missing mode stays Normal");
  for (const char* invalid : {"powerMode=1junk\n", "powerMode=-1\n", "powerMode=2\n", "powerMode=256\n"}) {
    runner.expectEq(int(PowerMode::Normal), int(decodeSettings(invalid, std::strlen(invalid), true).powerMode),
                    "mode_migration: invalid mode cannot enable standby");
  }
  ClockSettings value = old;
  value.powerMode = PowerMode::PowerSaving;
  value.face = Face::DayNight;
  char encoded[320];
  const size_t length = encodeSettings(value, encoded, sizeof(encoded));
  const auto decoded = decodeSettings(encoded, length, true);
  runner.expectEq(int(PowerMode::PowerSaving), int(decoded.powerMode), "mode survives text round trip");
  runner.expectEq(int(value.face), int(decoded.face), "mode preserves face");
  runner.expectEq(7, int(decoded.utcOffset), "mode preserves time zone");
  runner.expectFalse(decoded.use24h, "mode preserves time format");
  runner.expectEq(size_t{0}, encodeSettings(value, encoded, 12), "short output cannot publish settings");
  papyrix::hal::Storage storage;
  ClockSettings loaded;
  files.clear();
  runner.expectTrue(loadSettings(storage, loaded), "missing settings are usable defaults");
  runner.expectTrue(saveSettings(storage, value), "save complete settings");
  runner.expectTrue(loadSettings(storage, loaded), "load complete settings");
  runner.expectEq(int(PowerMode::PowerSaving), int(loaded.powerMode), "saved mode survives reopening");
  runner.expectEq(7, int(loaded.utcOffset), "saved zone survives reopening");

  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Close, Failure::Preserve, Failure::Publish,
                           Failure::Rollback, Failure::PreserveAfter, Failure::PublishAfter}) {
    files.clear();
    put(finalPath, legacy);
    failure = fault;
    runner.expectFalse(saveSettings(storage, value), "settings_replace_failure: report storage failure");
    failure = Failure::None;
    runner.expectTrue(loadSettings(storage, loaded), "settings_replace_failure: previous settings remain readable");
    runner.expectEq(int(PowerMode::Normal), int(loaded.powerMode), "settings_replace_failure: retain previous mode");
    runner.expectEq(7, int(loaded.utcOffset), "settings_replace_failure: retain previous zone");
  }
  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Close, Failure::Publish, Failure::PublishAfter}) {
    files.clear();
    failure = fault;
    runner.expectFalse(saveSettings(storage, value), "failed first settings save reports failure");
    failure = Failure::None;
    runner.expectTrue(loadSettings(storage, loaded), "failed first settings save leaves usable defaults");
    runner.expectEq(int(PowerMode::Normal), int(loaded.powerMode), "failed first save does not publish settings");
    runner.expectEq(int(Face::Big), int(loaded.face), "failed first save keeps the default face");
  }
  for (bool published : {false, true}) {
    files.clear();
    put(backupPath, legacy);
    encodeSettings(value, encoded, sizeof(encoded));
    put(published ? finalPath : tempPath, encoded);
    runner.expectTrue(loadSettings(storage, loaded), "restart recovers a complete file at each rename boundary");
    runner.expectEq(int(published ? PowerMode::PowerSaving : PowerMode::Normal), int(loaded.powerMode),
                    "restart chooses final before backup and never a temporary file");
    runner.expectTrue(saveSettings(storage, value), "a later save recovers interrupted publication");
  }
  failure = Failure::Read;
  runner.expectFalse(loadSettings(storage, loaded), "read failure is not a missing-file default");
  failure = Failure::None;
  put(finalPath, std::string(400, 'x'));
  runner.expectFalse(loadSettings(storage, loaded), "oversized settings cannot be accepted as a partial file");
  return runner.allPassed() ? 0 : 1;
}
