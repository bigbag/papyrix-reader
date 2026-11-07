#include <BootHardware.h>
#include <InputManager.h>
#include <MappedInputManager.h>
#include <PowerPolicy.h>
#include <ThemeManager.h>

#include <cassert>

#include "apps/ClockApp.h"
#include "apps/ClockStandby.h"
#include "core/Core.h"

InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);
ThemeManager::ThemeManager() : activeTheme(getBuiltinLightTheme()), themeName{} {}
ThemeManager& ThemeManager::instance() {
  static ThemeManager manager;
  return manager;
}

namespace papyrix {
Core core;
ContentHandle::ContentHandle() : type(ContentType::None) {}
ContentHandle::~ContentHandle() { assert(type == ContentType::None); }
Result<void> Core::init() {
  assert(false);
  return ErrVoid(Error::IOError);
}
namespace hal {
bool Clock::localTime(std::tm&) const { return false; }
Display::InitResult Display::recover() {
  assert(false);
  return InitResult::HardwareError;
}
}  // namespace hal
namespace board {
void earlyInit(const BoardProfile&, BootPurpose) { assert(false); }
void releaseDeepSleepHolds() { assert(false); }
void restoreRecoveryStorage(const BoardProfile&) { assert(false); }
}  // namespace board
namespace clock_app {
void enter(Core&) { assert(false); }
bool update(Core&) {
  assert(false);
  return false;
}
miniapps::Frame render(Core&, bool) {
  assert(false);
  return {};
}
miniapps::Action handleEvent(Core&, const Event&, bool) {
  assert(false);
  return miniapps::Action::None;
}
miniapps::ExitResult prepareExit(Core&, miniapps::ExitTarget) { return miniapps::ExitResult::Ready; }
void exit(Core&) { assert(false); }
bool standbyDue(Core&, uint32_t, bool) { return false; }
void resumeInteractive(Core&, const ResumeRecord*) { assert(false); }
StandbyResult enterStandby(Core&) {
  assert(false);
  return StandbyResult::DisplayError;
}
const char* standbyErrorMessage(StandbyResult) {
  assert(false);
  return "";
}
}  // namespace clock_app
#define INACTIVE_APP(name)                                        \
  namespace name {                                                \
  void enter(Core&) { assert(false); }                            \
  bool update(Core&) {                                            \
    assert(false);                                                \
    return false;                                                 \
  }                                                               \
  miniapps::Action handleEvent(Core&, const Event&, bool) {       \
    assert(false);                                                \
    return miniapps::Action::None;                                \
  }                                                               \
  miniapps::Frame render(Core&, bool) {                           \
    assert(false);                                                \
    return {};                                                    \
  }                                                               \
  miniapps::ExitResult prepareExit(Core&, miniapps::ExitTarget) { \
    assert(false);                                                \
    return miniapps::ExitResult::Ready;                           \
  }                                                               \
  void exit(Core&) { assert(false); }                             \
  }
INACTIVE_APP(imageviewer_app)
INACTIVE_APP(printer_app)
INACTIVE_APP(localsend_app)
INACTIVE_APP(pomodoro_app)
#ifdef TEST_GO_APP
INACTIVE_APP(game2048_app)
#else
INACTIVE_APP(go_app)
#endif
#undef INACTIVE_APP
}  // namespace papyrix
