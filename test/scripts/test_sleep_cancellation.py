#!/usr/bin/env python3
"""Run idle auto-sleep and cancellation through the firmware state machine."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <core/StateMachine.h>
#include <hal/Cpu.h>
#include <cassert>
#include <cstdio>
#include <Logging.h>
namespace papyrix {
struct Core {
  hal::Cpu cpu;
  struct Input {
    unsigned idle = 60000;
    bool suppressed = false;
    bool powerSuppressed() const { return suppressed; }
    unsigned idleTimeMs() const { return idle; }
    void resetIdleTimer() { idle = 0; }
  } input;
  struct Settings { unsigned getPowerButtonDuration() const { return 700; } } settings;
  struct Display { bool deepSleep() { return false; } } display;
  struct Buffers { char text[128]{}; } buf;
};
Core core;
class SleepState : public State {
 public:
  unsigned entries = 0;
  void enter(Core& core) override {
    ++entries;
    assert(!core.cpu.isThrottled());
    @FAILURE@
  }
  StateTransition update(Core& core) override;
  StateId id() const override { return StateId::Sleep; }
};
@UPDATE@
class ErrorState : public State {
 public:
  unsigned entries = 0;
  void enter(Core&) override { ++entries; }
  StateTransition update(Core&) override { return StateTransition::stay(StateId::Error); }
  StateId id() const override { return StateId::Error; }
};
}
class ReaderState : public papyrix::State {
 public:
  unsigned exits = 0;
  void exit(papyrix::Core& core) override {
    assert(!core.cpu.isThrottled());
    ++exits;
  }
  papyrix::StateTransition update(papyrix::Core&) override {
    return papyrix::StateTransition::stay(papyrix::StateId::Reader);
  }
  papyrix::StateId id() const override { return papyrix::StateId::Reader; }
};
papyrix::StateMachine stateMachine;
class AppLauncherState : public papyrix::State {
 public:
  bool allowSleep = false;
  unsigned updates = 0, renders = 0, requests = 0;
  bool requestSleep(papyrix::Core& core) {
    ++requests;
    if (!allowSleep) core.input.suppressed = true;
    return allowSleep;
  }
  papyrix::StateTransition update(papyrix::Core&) override {
    ++updates;
    return papyrix::StateTransition::stay(papyrix::StateId::AppLauncher);
  }
  void render(papyrix::Core&) override { ++renders; }
  papyrix::StateId id() const override { return papyrix::StateId::AppLauncher; }
} appLauncherState;
void poll() {
  const unsigned autoSleepTimeout = 1000;
  @AUTO_SLEEP@
  stateMachine.update(papyrix::core);
}
struct InputManager {
  static constexpr int BTN_POWER = 0;
  bool pressed = false;
  bool isPressed(int) const { return pressed; }
} inputManager;
void pollPower() {
  static unsigned long now = 1000;
  const unsigned long loopStartTime = now;
  now += 50;
  @POWER_SLEEP@
  stateMachine.update(papyrix::core);
}
int main() {
  papyrix::SleepState sleep;
  papyrix::ErrorState error;
  ReaderState reader;
  stateMachine.registerState(&reader);
  stateMachine.registerState(&sleep);
  stateMachine.registerState(&error);
  stateMachine.registerState(&appLauncherState);
  stateMachine.init(papyrix::core, papyrix::StateId::Reader);
  papyrix::core.cpu.throttle();
  for (int i = 0; i < 4; ++i) poll();
  assert(sleep.entries == 1 && error.entries == 1);
  assert(reader.exits == 1);
  papyrix::core.input.idle = 1000;
  papyrix::core.cpu.throttle();
  poll();
  poll();
  assert(sleep.entries == 2 && error.entries == 2);
  stateMachine.init(papyrix::core, papyrix::StateId::AppLauncher);
  papyrix::core.input.idle = 1000;
  poll();
  assert(stateMachine.isInState(papyrix::StateId::AppLauncher));
  assert(sleep.entries == 2 && appLauncherState.updates == 1 && appLauncherState.renders == 1);
  appLauncherState.allowSleep = true;
  poll();
  assert(stateMachine.isInState(papyrix::StateId::Sleep) && sleep.entries == 3);
  stateMachine.init(papyrix::core, papyrix::StateId::AppLauncher);
  papyrix::core.input.suppressed = false;
  appLauncherState.allowSleep = false;
  inputManager.pressed = true;
  const unsigned requests = appLauncherState.requests;
  const unsigned renders = appLauncherState.renders;
  for (int i = 0; i < 20; ++i) pollPower();
  assert(appLauncherState.requests == requests + 1);
  assert(appLauncherState.renders == renders + 20 && sleep.entries == 3);
  appLauncherState.allowSleep = true;
  for (int i = 0; i < 20; ++i) pollPower();
  assert(appLauncherState.requests == requests + 1 && sleep.entries == 3);
  inputManager.pressed = false;
  papyrix::core.input.suppressed = false;
  pollPower();
  inputManager.pressed = true;
  for (int i = 0; i < 20 && !stateMachine.isInState(papyrix::StateId::Sleep); ++i) pollPower();
  assert(stateMachine.isInState(papyrix::StateId::Sleep) && sleep.entries == 4);
}
'''


def main():
    sleep = (ROOT / "src/states/SleepState.cpp").read_text()
    start = sleep.index("  if (!core.display.deepSleep())")
    failure = sleep[start:sleep.index("\n  core.frontLight.shutdown(", start)]
    start = sleep.index("StateTransition SleepState::update(")
    update = sleep[start:sleep.index("\n\n", start)]
    loop = (ROOT / "src/main.cpp").read_text()
    start = loop.index("  if (autoSleepTimeout > 0")
    auto_sleep = loop[start:loop.index("\n\n", start)]
    start = loop.index("  // Power button sleep check:")
    power_sleep = loop[start:loop.index("\n  }\n", start) + 5]
    harness = (HARNESS.replace("@FAILURE@", failure)
               .replace("@UPDATE@", update).replace("@AUTO_SLEEP@", auto_sleep)
               .replace("@POWER_SLEEP@", power_sleep))
    with tempfile.TemporaryDirectory(prefix="papyrix-sleep-cancel-") as directory:
        path = Path(directory)
        (path / "sleep.cpp").write_text(harness)
        (path / "Arduino.h").write_text(
            "#pragma once\ninline bool setCpuFrequencyMhz(unsigned) { return true; }\n")
        (path / "Logging.h").write_text(
            "#pragma once\n#define LOG_INF(...)\n#define LOG_ERR(...)\n#define LOG_DBG(...)\n")
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", "-DPAPYRIX_TARGET_X4CLASSIC=1", "-DF_CPU=240000000",
            f"-I{path}", f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'lib/BoardSupport/include'}", f"-I{ROOT / 'lib/EInkDisplay/include'}",
            str(path / "sleep.cpp"), str(ROOT / "src/core/StateMachine.cpp"),
            str(ROOT / "src/hal/Cpu.cpp"),
            "-o", str(path / "sleep"),
        ], check=True)
        subprocess.run([str(path / "sleep")], check=True)
    print("PASS: auto-sleep restores CPU; failed game save keeps update/render active; cancelled sleep reaches Error")


if __name__ == "__main__":
    main()
