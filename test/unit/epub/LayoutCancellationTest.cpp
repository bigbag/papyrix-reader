#include <Page.h>

#include "parsers/ChapterHtmlSlimParser.h"
#include "parsers/LayoutCancellation.h"
#include "test_utils.h"

int main() {
  TestUtils::TestRunner runner("LayoutCancellation");
  runner.expectFalse(chapter_html::layoutWasAborted(true, false), "completed_layout_is_not_abort");
  runner.expectFalse(chapter_html::layoutWasAborted(false, true), "batch_stop_is_resumable");
  runner.expectTrue(chapter_html::layoutWasAborted(false, false), "external_stop_is_abort");
  runner.expectFalse(chapter_html::imageFailureShouldPersist(true),
                     "externally cancelled image failure is retryable");
  runner.expectTrue(chapter_html::imageFailureShouldPersist(false), "genuine image failure remains persistent");

  using CallbackSetter = void (ChapterHtmlSlimParser::*)(const std::function<bool()>&);
  const CallbackSetter setter = &ChapterHtmlSlimParser::setExternalAbortCallback;
  runner.expectTrue(setter != nullptr, "live_parser_exposes_callback_refresh");
  return runner.allPassed() ? 0 : 1;
}
