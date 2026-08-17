#pragma once

namespace chapter_html {
constexpr bool layoutWasAborted(bool layoutCompleted, bool batchStopRequested) {
  return !layoutCompleted && !batchStopRequested;
}

constexpr bool imageFailureShouldPersist(bool externallyAborted) { return !externallyAborted; }
}  // namespace chapter_html
