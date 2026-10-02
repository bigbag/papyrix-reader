#include "apps/PomodoroTimer.h"
#include "test_utils.h"

namespace {

using papyrix::pomodoro::Period;
using papyrix::pomodoro::RunState;
using papyrix::pomodoro::StartMode;
using papyrix::pomodoro::Timer;
using papyrix::pomodoro::kFocusMs;
using papyrix::pomodoro::kLongBreakMs;
using papyrix::pomodoro::kShortBreakMs;

int stateId(RunState state) { return static_cast<int>(state); }
int periodId(Period period) { return static_cast<int>(period); }
int modeId(StartMode mode) { return static_cast<int>(mode); }

void expectIdleFocus(TestUtils::TestRunner& runner, const Timer& timer, const char* name) {
  runner.expectEq(stateId(RunState::Idle), stateId(timer.run), name);
  runner.expectEq(periodId(Period::Focus), periodId(timer.period), name);
  runner.expectEq(0, static_cast<int>(timer.completedFocus), name);
  runner.expectEq(kFocusMs, timer.remainingMs, name);
}

void completeRunningPeriod(Timer& timer) { papyrix::pomodoro::tick(timer, timer.deadlineMs); }

}  // namespace

int main() {
  TestUtils::TestRunner runner("Pomodoro timer");
  Timer timer;

  expectIdleFocus(runner, timer, "new timer is idle focus");
  runner.expectEq(modeId(StartMode::Auto), modeId(timer.startMode), "new timer is auto");
  runner.expectEq(25u, papyrix::pomodoro::displayedMinutes(timer.remainingMs), "idle shows 25 minutes");

  char label[32];
  papyrix::pomodoro::writePeriodLabel(timer, label, sizeof(label));
  runner.expectEqual("Focus · session 1 of 4", label, "idle focus label");
  runner.expectEqual("Ready · Auto", papyrix::pomodoro::statusLabel(timer), "idle status");
  runner.expectEqual("Start", papyrix::pomodoro::thirdButtonLabel(timer), "idle button");

  papyrix::pomodoro::start(timer, 1000);
  runner.expectEq(stateId(RunState::Running), stateId(timer.run), "start runs focus");
  runner.expectEq(1000u + kFocusMs, timer.deadlineMs, "start sets the focus deadline");
  papyrix::pomodoro::pause(timer, 61000);
  const uint32_t frozen = timer.remainingMs;
  runner.expectEq(kFocusMs - 60000u, frozen, "pause stores the remaining time");
  papyrix::pomodoro::tick(timer, 61000u + 600000u);
  runner.expectEq(frozen, timer.remainingMs, "paused time does not move");
  papyrix::pomodoro::resume(timer, 7000);
  papyrix::pomodoro::tick(timer, 67000);
  runner.expectEq(frozen - 60000u, timer.remainingMs, "resume continues from the frozen time");

  timer = Timer{};
  timer.startMode = StartMode::Manual;
  papyrix::pomodoro::start(timer, 0);
  papyrix::pomodoro::tick(timer, kFocusMs + kShortBreakMs);
  runner.expectEq(stateId(RunState::Waiting), stateId(timer.run), "a late tick waits once");
  runner.expectEq(periodId(Period::ShortBreak), periodId(timer.period), "a late tick does not finish the break");
  runner.expectEq(kShortBreakMs, timer.remainingMs, "the next period keeps its full duration");

  timer = Timer{};
  timer.startMode = StartMode::Manual;
  for (int focus = 1; focus <= 3; ++focus) {
    papyrix::pomodoro::start(timer, 10);
    completeRunningPeriod(timer);
    runner.expectEq(stateId(RunState::Waiting), stateId(timer.run), "manual focus waits");
    runner.expectEq(periodId(Period::ShortBreak), periodId(timer.period), "the first breaks are short");
    runner.expectEq(focus, static_cast<int>(timer.completedFocus), "focus completion raises the count");
    papyrix::pomodoro::start(timer, 20);
    completeRunningPeriod(timer);
    runner.expectEq(periodId(Period::Focus), periodId(timer.period), "a short break returns to focus");
    runner.expectEq(focus, static_cast<int>(timer.completedFocus), "a short break keeps the count");
  }
  papyrix::pomodoro::start(timer, 30);
  completeRunningPeriod(timer);
  runner.expectEq(periodId(Period::LongBreak), periodId(timer.period), "the fourth focus waits for a long break");
  runner.expectEq(4, static_cast<int>(timer.completedFocus), "the fourth focus sets count 4");
  runner.expectEq(kLongBreakMs, timer.remainingMs, "the long break shows 15 minutes");
  papyrix::pomodoro::start(timer, 40);
  completeRunningPeriod(timer);
  runner.expectEq(periodId(Period::Focus), periodId(timer.period), "a long break waits for focus");
  runner.expectEq(0, static_cast<int>(timer.completedFocus), "a long break clears the count");

  timer = Timer{};
  papyrix::pomodoro::start(timer, 0);
  completeRunningPeriod(timer);
  runner.expectEq(stateId(RunState::Running), stateId(timer.run), "auto starts the next period");
  runner.expectEq(periodId(Period::ShortBreak), periodId(timer.period), "auto starts the short break");
  papyrix::pomodoro::start(timer, 1);
  runner.expectEq(periodId(Period::ShortBreak), periodId(timer.period), "start during running does nothing");

  timer = Timer{};
  timer.startMode = StartMode::Manual;
  papyrix::pomodoro::start(timer, 0);
  completeRunningPeriod(timer);
  papyrix::pomodoro::start(timer, 5);
  papyrix::pomodoro::skipBreak(timer, 6);
  runner.expectEq(stateId(RunState::Waiting), stateId(timer.run), "skip waits in manual mode");
  runner.expectEq(periodId(Period::Focus), periodId(timer.period), "skip ends the break");
  runner.expectEq(1, static_cast<int>(timer.completedFocus), "skip does not change the focus count");

  timer = Timer{};
  papyrix::pomodoro::start(timer, 50);
  papyrix::pomodoro::skipBreak(timer, 51);
  runner.expectEq(stateId(RunState::Running), stateId(timer.run), "skip during focus does nothing");
  runner.expectEq(periodId(Period::Focus), periodId(timer.period), "skip during focus keeps focus");

  timer.startMode = StartMode::Auto;
  papyrix::pomodoro::reset(timer);
  expectIdleFocus(runner, timer, "reset returns to idle focus");
  runner.expectEq(modeId(StartMode::Auto), modeId(timer.startMode), "reset keeps the start mode");
  papyrix::pomodoro::start(timer, 200);
  papyrix::pomodoro::pause(timer, 200);
  papyrix::pomodoro::reset(timer);
  expectIdleFocus(runner, timer, "reset from paused returns to idle focus");
  runner.expectEq(modeId(StartMode::Auto), modeId(timer.startMode), "reset from paused keeps auto");
  timer.startMode = StartMode::Manual;
  papyrix::pomodoro::start(timer, 210);
  completeRunningPeriod(timer);
  papyrix::pomodoro::reset(timer);
  expectIdleFocus(runner, timer, "reset from waiting returns to idle focus");
  papyrix::pomodoro::start(timer, 220);
  completeRunningPeriod(timer);
  papyrix::pomodoro::start(timer, 230);
  papyrix::pomodoro::reset(timer);
  expectIdleFocus(runner, timer, "reset from a break returns to idle focus");
  papyrix::pomodoro::skipBreak(timer, 240);
  expectIdleFocus(runner, timer, "skip during idle does nothing");
  runner.expectEq(25u, papyrix::pomodoro::displayedMinutes(kFocusMs), "25:00 shows 25");
  runner.expectEq(25u, papyrix::pomodoro::displayedMinutes(24u * 60u * 1000u + 1u), "24 minutes plus 1 ms shows 25");
  runner.expectEq(24u, papyrix::pomodoro::displayedMinutes(24u * 60u * 1000u), "24:00 shows 24");
  runner.expectEq(1u, papyrix::pomodoro::displayedMinutes(1), "1 ms shows 1 minute");

  timer = Timer{};
  timer.startMode = StartMode::Manual;
  papyrix::pomodoro::start(timer, 0xFFFF0000u);
  papyrix::pomodoro::tick(timer, timer.deadlineMs - 1u);
  runner.expectEq(stateId(RunState::Running), stateId(timer.run), "wrap still has time before the deadline");
  runner.expectEq(1u, papyrix::pomodoro::displayedMinutes(timer.remainingMs), "1 ms before the deadline shows 1");
  papyrix::pomodoro::tick(timer, timer.deadlineMs);
  runner.expectEq(stateId(RunState::Waiting), stateId(timer.run), "the deadline expires across the wrap");

  return runner.allPassed() ? 0 : 1;
}
