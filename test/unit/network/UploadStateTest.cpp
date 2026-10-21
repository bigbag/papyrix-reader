#include <SDCardManager.h>

#include "network/UploadState.h"
#include "test_utils.h"

using papyrix::UploadState;

static std::string contents(const char* path) {
  FsFile file = SdMan.open(path);
  std::string result;
  while (file.available()) result.push_back(static_cast<char>(file.read()));
  return result;
}

int main() {
  TestUtils::TestRunner runner("Staged uploads");
  for (const char* destination : {"/book.epub", "/.papyrix/locale.txt", "/firmware.bin"}) {
    SdMan.reset();
    SdMan.setFileData(destination, "original");
    UploadState upload;
    runner.expectTrue(upload.begin(destination), "replacement opens a temporary file");
    upload.write(reinterpret_cast<const uint8_t*>("replacement"), 11);
    runner.expectTrue(contents(destination) == "original", "transfer leaves the original unchanged");
    upload.abort();
    runner.expectTrue(contents(destination) == "original", "abort preserves the original");
    runner.expectTrue(SdMan.writtenFilePaths().empty(), "abort removes its temporary file");

    runner.expectTrue(upload.begin(destination), "upload can restart after abort");
    upload.write(reinterpret_cast<const uint8_t*>("replacement"), 11);
    upload.finish();
    runner.expectTrue(upload.success && contents(destination) == "replacement",
                      "complete upload replaces the original");
    upload.abort();
    runner.expectTrue(contents(destination) == "replacement", "late abort preserves a completed upload");
    runner.expectTrue(SdMan.writtenFilePaths() == std::vector<std::string>{destination},
                      "successful commit leaves only the completed destination");
  }

  for (const bool shortWrite : {false, true}) {
    SdMan.reset();
    SdMan.setFileData("/book.epub", "original");
    if (shortWrite)
      SdMan.setWriteLimit(3);
    else
      SdMan.setSyncResult(false);
    UploadState upload;
    runner.expectTrue(upload.begin("/book.epub"), "failure fixture starts upload");
    upload.write(reinterpret_cast<const uint8_t*>("replacement"), 11);
    upload.finish();
    runner.expectTrue(!upload.success && !upload.error.isEmpty(), "failed write or sync rejects the upload");
    runner.expectTrue(contents("/book.epub") == "original", "failed write or sync preserves original bytes");
    runner.expectTrue(SdMan.writtenFilePaths().empty(), "failed transfer removes temporary data");
  }

  SdMan.reset();
  SdMan.setFileData("/book.epub", "original");
  {
    UploadState upload;
    runner.expectTrue(upload.begin("/book.epub"), "replacement starts before rename failure");
    upload.write(reinterpret_cast<const uint8_t*>("replacement"), 11);
    SdMan.setRenameResult(false);
    upload.finish();
    runner.expectTrue(!upload.success && contents("/book.epub") == "original", "failed backup rename keeps original");
    runner.expectTrue(SdMan.writtenFilePaths().empty(), "failed commit removes staging data");
  }

  SdMan.reset();
  SdMan.setFileData("/book.epub", "original");
  {
    UploadState upload;
    runner.expectTrue(upload.begin("/book.epub"), "replacement starts before commit failure");
    upload.write(reinterpret_cast<const uint8_t*>("replacement"), 11);
    SdMan.setRenameFailOnCall(2);
    upload.finish();
    runner.expectTrue(!upload.success && contents("/book.epub") == "original",
                      "failed publication restores the original from backup");
    runner.expectTrue(SdMan.writtenFilePaths().empty(), "rollback removes the incomplete replacement");
  }

  // FatFile::rename removes the old entry before its final cacheSync can fail,
  // so a false return can leave the move already done.
  SdMan.reset();
  SdMan.setFileData("/book.epub", "original");
  {
    UploadState upload;
    runner.expectTrue(upload.begin("/book.epub"), "upload starts before a lying backup rename");
    upload.write(reinterpret_cast<const uint8_t*>("replacement"), 11);
    SdMan.queueRenameFailure(1, RenameFailMode::FailAfterMutation);
    upload.finish();
    runner.expectTrue(!upload.success && contents("/book.epub") == "original",
                      "failed backup rename restores the original bytes");
    runner.expectTrue(SdMan.writtenFilePaths().empty(), "restored upload removes staging data");
  }

  SdMan.reset();
  SdMan.setFileData("/book.epub", "original");
  {
    UploadState upload;
    runner.expectTrue(upload.begin("/book.epub"), "upload starts before an unrestorable rename");
    upload.write(reinterpret_cast<const uint8_t*>("replacement"), 11);
    SdMan.queueRenameFailure(1, RenameFailMode::FailAfterMutation);
    SdMan.queueRenameFailure(2, RenameFailMode::FailBeforeMutation);
    upload.finish();
    runner.expectTrue(!upload.success && upload.error.startsWith("Original file retained at "),
                      "unrestorable original is reported at its backup path");
    const String backup = upload.error.substring(sizeof("Original file retained at ") - 1);
    runner.expectTrue(!SdMan.exists("/book.epub"), "moved original leaves the destination free");
    runner.expectTrue(SdMan.exists(backup.c_str()) && contents(backup.c_str()) == "original",
                      "reported backup path exists and holds the original bytes");
    runner.expectTrue(SdMan.writtenFilePaths().empty(), "retained-backup failure removes staging data");
  }

  SdMan.reset();
  SdMan.setFileData("/book.epub", "original");
  {
    UploadState upload;
    runner.expectTrue(upload.begin("/book.epub"), "upload starts before two lying renames");
    upload.write(reinterpret_cast<const uint8_t*>("replacement"), 11);
    SdMan.queueRenameFailure(1, RenameFailMode::FailAfterMutation);
    SdMan.queueRenameFailure(2, RenameFailMode::FailAfterMutation);
    upload.finish();
    runner.expectTrue(!upload.success && contents("/book.epub") == "original",
                      "restore that reports failure still preserves the original bytes");
    runner.expectTrue(SdMan.writtenFilePaths().empty(), "twice-failed rename removes staging data");
  }

  SdMan.reset();
  SdMan.setFileData("/book.epub", "original");
  {
    UploadState upload;
    runner.expectTrue(upload.begin("/book.epub"), "upload starts before a lying rollback");
    upload.write(reinterpret_cast<const uint8_t*>("replacement"), 11);
    SdMan.queueRenameFailure(2, RenameFailMode::FailBeforeMutation);
    SdMan.queueRenameFailure(3, RenameFailMode::FailAfterMutation);
    upload.finish();
    runner.expectTrue(!upload.success && contents("/book.epub") == "original" &&
                          !upload.error.startsWith("Original file retained at "),
                      "rollback preserves the original without reporting a missing backup");
    runner.expectTrue(SdMan.writtenFilePaths().empty(), "lying rollback removes staging data");
  }

  SdMan.reset();
  SdMan.setFileData("/book.epub", "original");
  String retainedBackup;
  {
    UploadState upload;
    runner.expectTrue(upload.begin("/book.epub"), "upload starts before a lying publication");
    upload.write(reinterpret_cast<const uint8_t*>("replacement"), 11);
    SdMan.queueRenameFailure(2, RenameFailMode::FailAfterMutation);
    SdMan.queueRenameFailure(3, RenameFailMode::FailBeforeMutation);
    upload.finish();
    runner.expectTrue(!upload.success && upload.error.startsWith("Original file retained at "),
                      "lying publication keeps the original discoverable at the backup");
    retainedBackup = upload.error.substring(sizeof("Original file retained at ") - 1);
    runner.expectTrue(contents("/book.epub") == "replacement", "published replacement bytes stay in place");
    runner.expectTrue(SdMan.exists(retainedBackup.c_str()) && contents(retainedBackup.c_str()) == "original",
                      "backup keeps the original copy");
  }
  runner.expectTrue(contents("/book.epub") == "replacement" && SdMan.exists(retainedBackup.c_str()) &&
                        contents(retainedBackup.c_str()) == "original",
                    "teardown preserves the published replacement and the retained original");

  SdMan.reset();
  SdMan.setFileData("/book.epub", "original");
  {
    UploadState upload;
    runner.expectTrue(upload.begin("/book.epub"), "upload starts before a new request");
    upload.write(reinterpret_cast<const uint8_t*>("partial"), 7);
    upload.reset();
    runner.expectTrue(contents("/book.epub") == "original", "new request discards only the old partial");
    runner.expectTrue(SdMan.writtenFilePaths().empty(), "reset removes staging data");
    runner.expectTrue(upload.begin("/other.epub"), "new upload can target another file");
    upload.write(reinterpret_cast<const uint8_t*>("partial"), 7);
  }
  runner.expectTrue(!SdMan.exists("/other.epub"), "destruction does not publish an incomplete file");
  runner.expectTrue(SdMan.writtenFilePaths().empty(), "destruction removes staging data");

  SdMan.reset();
  SdMan.registerDirectory("/book.epub", {});
  UploadState upload;
  runner.expectFalse(upload.begin("/book.epub"), "upload cannot replace a directory");
  runner.expectTrue(SdMan.open("/book.epub").isDirectory(), "rejected upload preserves the directory");
  return runner.allPassed() ? 0 : 1;
}
