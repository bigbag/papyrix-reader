#include <SDCardManager.h>
#include <SdFat.h>

#include <memory>
#include <string>

#include "test_utils.h"

namespace {

std::string fileName(const FsFile& file) {
  char name[32] = {};
  file.getName(name, sizeof(name));
  return name;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("StorageMock");

  FsFile file;
  file.setDirectoryEntry({"chapter", true});
  file.close();
  runner.expectFalse(file.isDirectory(), "close clears directory flag");
  runner.expectEq(std::string(), fileName(file), "close clears entry name");

  file.setDirectoryEntry({"old", true});
  file.setBuffer("data");
  runner.expectEq(std::string(), fileName(file), "buffer setup clears entry name");

  file.setDirectoryEntry({"old", true});
  file.setSharedBuffer(std::make_shared<std::string>());
  runner.expectEq(std::string(), fileName(file), "shared buffer setup clears entry name");

  file.setDirectoryEntry({"old", true});
  file.setDirectory({});
  runner.expectEq(std::string(), fileName(file), "directory setup clears entry name");

  SdMan.reset();
  SdMan.registerDirectory("/old", {});
  runner.expectTrue(SdMan.rename("/old", "/new"), "directory rename succeeds");
  runner.expectFalse(SdMan.exists("/old"), "directory rename removes old path");
  runner.expectTrue(SdMan.exists("/new"), "directory rename creates new path");

  SdMan.reset();
  SdMan.registerDirectory("/same", {});
  runner.expectTrue(SdMan.rename("/same", "/same"), "same-path directory rename succeeds");
  runner.expectTrue(SdMan.exists("/same"), "same-path directory rename preserves directory");
  runner.expectFalse(SdMan.rename("/missing", "/missing"), "same-path rename rejects missing source");

  return runner.allPassed() ? 0 : 1;
}
