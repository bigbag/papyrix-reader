#include "test_utils.h"

#include "core/ExitToUiTransition.h"

using papyrix::ExitToUiMode;
using papyrix::ReturnTo;
using papyrix::planExitToUi;

int main() {
  TestUtils::TestRunner runner("ExitToUiTransitionTest");

  // Graceful stop with a cached transition keeps the transition destination
  {
    const auto plan = planExitToUi(true, true, ReturnTo::FILE_MANAGER, false);
    runner.expectTrue(plan.mode == ExitToUiMode::Graceful, "graceful: mode");
    runner.expectTrue(plan.returnTo == ReturnTo::FILE_MANAGER, "graceful: keeps transition returnTo");
  }

  // Graceful stop without transition, FileList source falls back to FILE_MANAGER
  {
    const auto plan = planExitToUi(true, false, ReturnTo::RECENT, true);
    runner.expectTrue(plan.mode == ExitToUiMode::Graceful, "fallback: mode");
    runner.expectTrue(plan.returnTo == ReturnTo::FILE_MANAGER, "fallback: FILE_MANAGER for FileList source");
  }

  // Graceful stop without transition, non-FileList source defaults to HOME
  {
    const auto plan = planExitToUi(true, false, ReturnTo::RECENT, false);
    runner.expectTrue(plan.returnTo == ReturnTo::HOME, "fallback: HOME otherwise");
  }

  // Emergency: stop timed out — mode must forbid progress/bookmark writes
  {
    const auto plan = planExitToUi(false, true, ReturnTo::HOME, false);
    runner.expectTrue(plan.mode == ExitToUiMode::Emergency, "emergency: mode");
    runner.expectTrue(plan.returnTo == ReturnTo::HOME, "emergency: preserves transition returnTo");
  }

  // Emergency without transition still resolves a destination for the boot marker
  {
    const auto plan = planExitToUi(false, false, ReturnTo::HOME, true);
    runner.expectTrue(plan.mode == ExitToUiMode::Emergency, "emergency fallback: mode");
    runner.expectTrue(plan.returnTo == ReturnTo::FILE_MANAGER, "emergency fallback: FILE_MANAGER");
  }

  return runner.allPassed() ? 0 : 1;
}
