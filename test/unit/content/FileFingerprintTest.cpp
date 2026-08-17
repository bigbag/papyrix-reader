#include <cstdint>
#include <string>

#include "FileFingerprint.h"
#include "SDCardManager.h"
#include "test_utils.h"

int main() {
  TestUtils::TestRunner runner("FileFingerprint");

  SdMan.reset();
  std::string original(4096, 'a');
  SdMan.registerFile("/books/book.epub", original);
  SdMan.setFileModifyDateTime("/books/book.epub", 0x5820, 0x7A00);

  testResetDelayStats();
  const uint32_t first = papyrix::fileFingerprint("/books/book.epub");
  runner.expectTrue(first != 0, "existing file has a fingerprint");
  runner.expectEq(static_cast<uint32_t>(0), testDelayCallCount(), "successful first attempt has no delay");
  runner.expectEq(first, papyrix::fileFingerprint("/books/book.epub"), "fingerprint is deterministic");

  std::string replacement = original;
  replacement[2000] = 'b';
  SdMan.registerFile("/books/book.epub", replacement);
  runner.expectTrue(first != papyrix::fileFingerprint("/books/book.epub"),
                    "sampled same-size replacement changes fingerprint");

  std::string unsampled = original;
  unsampled[128] = 'b';
  SdMan.registerFile("/books/book.epub", unsampled);
  runner.expectEq(first, papyrix::fileFingerprint("/books/book.epub"),
                  "sampling remains explicitly best effort");

  SdMan.setFileModifyDateTime("/books/book.epub", 0x5821, 0x7C00);
  runner.expectTrue(first != papyrix::fileFingerprint("/books/book.epub"),
                    "FAT modification time detects an unsampled replacement");

  testResetDelayStats();
  SdMan.setOpenFileForReadFailCount(2);
  runner.expectTrue(papyrix::fileFingerprint("/books/book.epub") != 0,
                    "fingerprint retries transient open failures");
  runner.expectEq(static_cast<uint32_t>(2), testDelayCallCount(), "retry delays occur between failed attempts");
  runner.expectEq(static_cast<uint32_t>(100), testDelayTotalMs(), "retry delays remain short and bounded");

  runner.expectEq(static_cast<uint32_t>(0), papyrix::fileFingerprint("/books/missing.epub"),
                  "missing file has no fingerprint");
  runner.expectEq(static_cast<uint32_t>(0), papyrix::fileFingerprint(nullptr),
                  "null path has no fingerprint");
  runner.expectEq(static_cast<uint32_t>(0), papyrix::fileFingerprint(""),
                  "empty path has no fingerprint");

  FsFile writable = SdMan.open("/books/book.epub", O_RDWR);
  uint16_t modifyDate = 0;
  uint16_t modifyTime = 0;
  runner.expectTrue(writable.getModifyDateTime(&modifyDate, &modifyTime),
                    "write-mode mock exposes the file modification time");
  writable.close();
  runner.expectFalse(writable.getModifyDateTime(&modifyDate, &modifyTime),
                     "closed mock file invalidates modification time access");

  SdMan.setReadLimit(64);
  testResetDelayStats();
  runner.expectEq(static_cast<uint32_t>(0), papyrix::fileFingerprint("/books/book.epub"),
                  "fingerprint rejects repeated mid-sample read failures");
  runner.expectEq(static_cast<uint32_t>(2), testDelayCallCount(),
                  "mid-sample failures delay only between attempts");
  SdMan.reset();

  const uint32_t stableFingerprint = 0x12345678u;
  runner.expectEq(stableFingerprint, papyrix::fingerprintOrSession(stableFingerprint),
                  "stable fingerprint is preserved");
  const uint32_t fallbackA = papyrix::fingerprintOrSession(0);
  const uint32_t fallbackB = papyrix::fingerprintOrSession(0);
  runner.expectTrue(fallbackA != 0 && fallbackB != 0 && fallbackA != fallbackB,
                    "failed fingerprints receive rotating session identities");

  const uint32_t sessionA = papyrix::sessionCacheFingerprint();
  const uint32_t sessionB = papyrix::sessionCacheFingerprint();
  runner.expectTrue(sessionA != 0 && sessionB != 0 && sessionA != sessionB,
                    "session cache fingerprints are nonzero and rotate");

  return runner.allPassed() ? 0 : 1;
}
