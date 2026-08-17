#pragma once

#include <cstdint>

#include "BootMode.h"

namespace papyrix {

enum class ExitToUiMode : uint8_t {
  Graceful,   // background task stopped: flush session, save progress, restart
  Emergency,  // stop timed out: task still owns cache files — restart without SD writes
};

struct ExitToUiPlan {
  ExitToUiMode mode = ExitToUiMode::Graceful;
  ReturnTo returnTo = ReturnTo::HOME;
};

// Pure decision for ReaderState::exitToUI. The return destination prefers a
// cached transition, then the FileList source state, then HOME. Emergency mode
// is chosen when the background cache task failed to stop; the caller must
// then restart without progress/bookmark/cache writes because the task still
// references them.
ExitToUiPlan planExitToUi(bool cacheStopSucceeded, bool transitionValid, ReturnTo transitionReturnTo,
                          bool sourceIsFileList);

}  // namespace papyrix
