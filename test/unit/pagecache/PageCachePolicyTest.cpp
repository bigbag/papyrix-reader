#include <PageCachePolicy.h>

#include <cstdint>

#include "test_utils.h"

int main() {
  TestUtils::TestRunner runner("PageCachePolicy");

  runner.expectFalse(page_cache::needsExtension(2, false, 0), "complete cache never extends");
  runner.expectTrue(page_cache::needsExtension(0, true, 0), "empty partial cache extends");
  runner.expectTrue(page_cache::needsExtension(1, true, 0), "one-page partial cache extends");
  runner.expectTrue(page_cache::needsExtension(2, true, 0), "two-page partial cache extends");
  runner.expectTrue(page_cache::needsExtension(3, true, 0), "threshold-sized partial cache extends");
  runner.expectFalse(page_cache::needsExtension(4, true, 0), "page before threshold does not extend");
  runner.expectTrue(page_cache::needsExtension(4, true, 1), "page at threshold extends");
  runner.expectFalse(page_cache::needsExtension(UINT16_MAX, true, 0), "large partial cache stays below threshold");
  runner.expectTrue(page_cache::needsExtension(UINT16_MAX, true, UINT16_MAX - 3),
                    "large partial cache extends without arithmetic wrap");
  runner.expectTrue(page_cache::needsExtension(UINT32_MAX, true, UINT32_MAX),
                    "maximum current page still requests extension");
  runner.expectTrue(page_cache::needsExtension(UINT32_MAX, true, UINT32_MAX - 2),
                    "threshold comparison does not wrap");

  runner.expectFalse(page_cache::hotExtendShouldCommit(false, 0),
                     "failed hot extend without progress is not committed");
  runner.expectTrue(page_cache::hotExtendShouldCommit(false, 1), "failed hot extend preserves completed pages");
  runner.expectTrue(page_cache::hotExtendShouldCommit(true, 0),
                    "successful hot extend may mark content complete without new pages");
  runner.expectTrue(page_cache::hotExtendIsPartial(false, false),
                    "failed hot extend remains partial despite parser state");
  runner.expectTrue(page_cache::hotExtendIsPartial(true, true), "successful hot extend follows parser continuation");
  runner.expectFalse(page_cache::hotExtendIsPartial(true, false), "successful completed hot extend is not partial");

  runner.expectTrue(page_cache::fullIndexCacheAction(false, false) == page_cache::FullIndexCacheAction::Create,
                    "missing cache is created during full indexing");
  runner.expectTrue(page_cache::fullIndexCacheAction(true, true) == page_cache::FullIndexCacheAction::Extend,
                    "partial cache is resumed during full indexing");
  runner.expectTrue(page_cache::fullIndexCacheAction(true, false) == page_cache::FullIndexCacheAction::Skip,
                    "complete cache is skipped during full indexing");

  runner.expectTrue(page_cache::proactiveExtensionAllowed(true, 50000),
                    "resumable hot extension is always allowed");
  runner.expectTrue(page_cache::proactiveExtensionAllowed(false, 999),
                    "small proactive cold extension is allowed");
  runner.expectFalse(page_cache::proactiveExtensionAllowed(false, 1000),
                     "large proactive cold extension is deferred");

  runner.expectTrue(page_cache::extensionChunk(1000, 5) == 50,
                    "cold cache beyond one thousand pages remains extendable");
  runner.expectTrue(page_cache::extensionChunk(UINT32_MAX - 10, 5) == 10,
                    "extension chunk clamps at the page-count format limit");
  runner.expectTrue(page_cache::extensionChunk(UINT32_MAX, 5) == 0,
                    "cache at the page-count format limit cannot overflow");

  runner.expectTrue(page_cache::backgroundShouldExtend(true, true, true, 78, 2),
                    "hot partial cache extends far from boundary");
  runner.expectFalse(page_cache::backgroundShouldExtend(true, true, false, 78, 2),
                     "cold partial cache waits far from boundary");
  runner.expectTrue(page_cache::backgroundShouldExtend(true, true, false, 78, 75),
                    "cold partial cache extends at boundary");

  runner.expectTrue(page_cache::backgroundWorkPending(false, false, true, true, false, 0, 0),
                    "missing cache needs background creation");
  runner.expectTrue(page_cache::backgroundWorkPending(true, true, true, true, true, 78, 2),
                    "hot partial cache needs background read-ahead");
  runner.expectFalse(page_cache::backgroundWorkPending(true, true, true, true, false, 78, 2),
                     "cold partial cache does not restart far from boundary");
  runner.expectTrue(page_cache::backgroundWorkPending(true, true, true, true, false, 78, 75),
                    "cold partial cache restarts at boundary");
  runner.expectTrue(page_cache::backgroundWorkPending(true, false, true, false, false, 0, 0),
                    "complete cache still needs a missing cover");
  runner.expectTrue(page_cache::backgroundWorkPending(true, false, false, true, false, 0, 0),
                    "complete cache still needs a missing thumbnail");
  runner.expectFalse(page_cache::backgroundWorkPending(true, false, true, true, false, 0, 0),
                     "complete cache and image assets need no background work");
  runner.expectTrue(page_cache::backgroundWorkPending(false, false, false, true, false, 0, 0, false),
                    "missing XTC thumbnail keeps work pending");
  runner.expectTrue(page_cache::backgroundWorkPending(false, false, true, false, false, 0, 0, false),
                    "missing XTC cover keeps work pending");
  runner.expectFalse(page_cache::backgroundWorkPending(false, false, true, true, false, 0, 0, false),
                     "completed XTC image assets have no cache work");

  runner.expectFalse(page_cache::lutFitsFile(UINT32_C(1) << 30, 0),
                     "corrupt page count cannot wrap its required LUT byte size");
  runner.expectTrue(page_cache::lutFitsFile(70000, UINT64_C(70000) * sizeof(uint32_t)),
                    "wide valid LUT fits its exact file span");
  runner.expectFalse(page_cache::lutFitsFile(70000, UINT64_C(70000) * sizeof(uint32_t) - 1),
                     "wide LUT missing one byte is rejected");

  return runner.allPassed() ? 0 : 1;
}
