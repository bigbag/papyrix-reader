#include "test_utils.h"

#include "core/StateMachine.h"

namespace papyrix {

struct Core {};

class TestState final : public State {
 public:
  explicit TestState(StateId id) : id_(id), next_(StateTransition::stay(id)) {}

  void enter(Core&) override { ++enterCount_; }
  StateTransition update(Core&) override { return next_; }
  StateId id() const override { return id_; }

  void transitionTo(StateId next) { next_ = StateTransition::to(next); }
  int enterCount() const { return enterCount_; }

 private:
  StateId id_;
  StateTransition next_;
  int enterCount_ = 0;
};

}  // namespace papyrix

using namespace papyrix;

int main() {
  TestUtils::TestRunner runner("StateMachineTest");

  Core core;
  StateMachine stateMachine;
  TestState startup(StateId::Startup);
  TestState home(StateId::Home);
  TestState fileList(StateId::FileList);
  TestState recent(StateId::Recent);
  TestState reader(StateId::Reader);
  TestState settings(StateId::Settings);
  TestState network(StateId::Network);
  TestState calibreSync(StateId::CalibreSync);
  TestState appLauncher(StateId::AppLauncher);
  TestState sleep(StateId::Sleep);
  TestState error(StateId::Error);

  State* const uiStates[] = {&startup,     &home,  &fileList,    &recent, &reader, &settings,
                            &network,     &calibreSync, &appLauncher, &sleep,  &error};
  for (State* state : uiStates) {
    stateMachine.registerState(state);
  }

  stateMachine.init(core, StateId::Home);
  home.transitionTo(StateId::Error);
  stateMachine.update(core);

  runner.expectTrue(stateMachine.isInState(StateId::Error), "eleventh registered state can become current");
  runner.expectEq(1, error.enterCount(), "eleventh registered state is entered");

  return runner.allPassed() ? 0 : 1;
}
