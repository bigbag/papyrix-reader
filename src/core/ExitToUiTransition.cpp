#include "ExitToUiTransition.h"

namespace papyrix {

ExitToUiPlan planExitToUi(const bool cacheStopSucceeded, const bool transitionValid, const ReturnTo transitionReturnTo,
                          const bool sourceIsFileList) {
  ExitToUiPlan plan;
  if (transitionValid) {
    plan.returnTo = transitionReturnTo;
  } else if (sourceIsFileList) {
    plan.returnTo = ReturnTo::FILE_MANAGER;
  }
  plan.mode = cacheStopSucceeded ? ExitToUiMode::Graceful : ExitToUiMode::Emergency;
  return plan;
}

}  // namespace papyrix
