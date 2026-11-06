#include <MiniApps.h>

#include <cassert>
#include <cstdio>

namespace {
using namespace miniapps;
struct Session {
  unsigned score = 64;
  unsigned savedScore = 0;
  unsigned now = 0;
  unsigned displayedTime = 0;
  ExitResult saveResult = ExitResult::Ready;
};
using TestApp = App<Session, Action>;
using TestHost = Host<Session, Action>;

const TestApp app = {"Session",
                     nullptr,
                     [](Session& session) {
                       const bool changed = session.displayedTime != session.now;
                       session.displayedTime = session.now;
                       return changed;
                     },
                     [](Session&, const Action& action, bool) { return action; },
                     [](Session&, bool) { return Frame{Refresh::Fast, true}; },
                     [](Session& session) { session.score = 0; },
                     [](Session& session, ExitTarget) {
                       if (session.saveResult == ExitResult::Ready) session.savedScore = session.score;
                       return session.saveResult;
                     },
                     false,
                     true};

void blockedSleepRetainsItsDestination() {
  Session session;
  TestHost host;
  host.start(session, app);
  session.saveResult = ExitResult::Blocked;
  assert(!host.prepareExit(session, ExitTarget::Sleep));
  assert(host.active() && host.pendingExit() == ExitTarget::Sleep);
  assert(session.score == 64 && session.savedScore == 0);
  assert(host.keepAwake());
  session.saveResult = ExitResult::Ready;
  assert(host.handleEvent(session, Action::RetryExit) == ExitTarget::Sleep);
  assert(!host.active() && session.savedScore == 64 && session.score == 0);
}

void cancelledExitDoesNotLoseTheSession() {
  Session session;
  TestHost host;
  host.start(session, app);
  session.saveResult = ExitResult::Blocked;
  assert(host.handleEvent(session, Action::Exit) == ExitTarget::None);
  host.handleEvent(session, Action::CancelExit);
  assert(host.active() && host.pendingExit() == ExitTarget::None);
  assert(session.score == 64 && !host.keepAwake());
  session.saveResult = ExitResult::Ready;
  assert(host.requestExit(session, ExitTarget::Apps) == ExitTarget::Apps);
  assert(session.savedScore == 64);
}

void discardDoesNotRetryAFailedSave() {
  Session session;
  TestHost host;
  host.start(session, app);
  session.saveResult = ExitResult::Blocked;
  assert(!host.prepareExit(session, ExitTarget::Sleep));
  assert(host.handleEvent(session, Action::DiscardExit) == ExitTarget::Sleep);
  assert(!host.active() && session.savedScore == 0);
}

void settingsFailureKeepsTheMenuEditable() {
  Session session;
  TestHost host;
  host.start(session, app);
  session.saveResult = ExitResult::OpenMenu;
  assert(host.requestExit(session, ExitTarget::Apps) == ExitTarget::None);
  assert(host.active() && host.menuOpen() && host.pendingExit() == ExitTarget::None);
  session.score = 128;
  host.handleEvent(session, Action::CloseMenu);
  assert(host.menuOpen() && session.savedScore == 0);
  session.saveResult = ExitResult::Ready;
  host.handleEvent(session, Action::CloseMenu);
  assert(host.active() && !host.menuOpen() && session.savedScore == 128);
}

void menuSuspendsAppUpdatesButNotElapsedTime() {
  Session session;
  TestHost host;
  host.start(session, app);
  host.handleEvent(session, Action::OpenMenu);
  session.now = 60;
  host.update(session);
  assert(session.displayedTime == 0);
  host.handleEvent(session, Action::CloseMenu);
  host.update(session);
  assert(session.displayedTime == 60 && host.needsRender());
}

void retryAndDiscardRequireAPendingExit() {
  Session session;
  TestHost host;
  host.start(session, app);
  host.handleEvent(session, Action::RetryExit);
  host.handleEvent(session, Action::DiscardExit);
  assert(host.active() && session.score == 64 && session.savedScore == 0);
}
}  // namespace

int main() {
  blockedSleepRetainsItsDestination();
  cancelledExitDoesNotLoseTheSession();
  discardDoesNotRetryAFailedSave();
  settingsFailureKeepsTheMenuEditable();
  menuSuspendsAppUpdatesButNotElapsedTime();
  retryAndDiscardRequireAPendingExit();
  std::puts("MiniAppHostTest: exit and menu transitions pass");
}
