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

  // Missing source under a distinct destination fails (matches SdFat)
  SdMan.reset();
  SdMan.registerFile("/dst.bin", "existing");
  runner.expectFalse(SdMan.rename("/missing.bin", "/dst.bin"), "missing-source rename fails");
  runner.expectTrue(SdMan.exists("/dst.bin"), "missing-source rename preserves destination");

  // commitFile with a missing temp file must fail (previously the lenient
  // rename reported success). The destination is removed by design: the real
  // commitFile removes the target first so a failed publish leaves no file
  // rather than a stale one ("complete file or none" on power loss).
  SdMan.reset();
  SdMan.registerFile("/final.bmp", "keep");
  runner.expectFalse(SdMan.commitFile("/gone.part", "/final.bmp"), "missing-part commitFile fails");
  runner.expectFalse(SdMan.exists("/final.bmp"), "missing-part commitFile leaves no stale destination");

  // Normal publish still works and consumes the temporary file
  SdMan.reset();
  SdMan.registerFile("/ok.part", "data");
  runner.expectTrue(SdMan.commitFile("/ok.part", "/ok.bmp"), "normal commitFile succeeds");
  runner.expectFalse(SdMan.exists("/ok.part"), "normal commitFile consumes temporary file");
  runner.expectTrue(SdMan.exists("/ok.bmp"), "normal commitFile publishes destination");

  return runner.allPassed() ? 0 : 1;
}
