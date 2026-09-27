#include <ContentParser.h>
#include <Page.h>
#include <PageCache.h>
#include <RenderConfig.h>
#include <SDCardManager.h>
#include <esp_task_wdt.h>

#include <memory>

#include "test_utils.h"

void ImageBlock::render(GfxRenderer&, int, int, int) const {}
bool ImageBlock::serialize(FsFile&) const { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile&) { return nullptr; }

namespace {
class SlowParser final : public ContentParser {
 public:
  bool parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint32_t maxPages,
                  const AbortCallback& shouldAbort) override {
    while (emitted_ < 50 && (maxPages == 0 || emitted_ < maxPages)) {
      if (shouldAbort && shouldAbort()) return false;
      MockTaskWdt::advance(300);
      onPageComplete(std::make_unique<Page>());
      ++emitted_;
    }
    return true;
  }

  bool hasMoreContent() const override { return emitted_ < 50; }
  void reset() override { emitted_ = 0; }

 private:
  uint32_t emitted_ = 0;
};
}  // namespace

int main() {
  TestUtils::TestRunner runner("PageCacheWatchdog");
  const RenderConfig config{};
  for (int status : {ESP_ERR_NOT_FOUND, ESP_ERR_INVALID_STATE, ESP_OK}) {
    SdMan.reset();
    MockTaskWdt::clear(status);
    SlowParser parser;
    PageCache cache("/cache/watchdog.bin");
    runner.expectTrue(cache.create(parser, config, 50), "slow book cache completes");
    runner.expectEq(0u, MockTaskWdt::rejectedResets, "cache generation makes no invalid watchdog reset requests");
    runner.expectFalse(MockTaskWdt::timedOut, "cache generation keeps a subscribed task within its watchdog deadline");

    PageCache loaded("/cache/watchdog.bin");
    runner.expectTrue(loaded.load(config), "book cache remains readable with each watchdog state");
    runner.expectEq(uint32_t{50}, loaded.pageCount(), "cache retains all source pages");
    runner.expectFalse(loaded.isPartial(), "cache records the end of the book");
    runner.expectTrue(loaded.loadPage(49) != nullptr, "last source page remains readable");
  }
  return runner.allPassed() ? 0 : 1;
}
