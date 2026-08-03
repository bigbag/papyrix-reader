#pragma once

#include "../states/State.h"
#include "Types.h"

namespace papyrix {

struct Core;

class StateMachine {
 public:
  void init(Core& core, StateId initialState = StateId::Startup);
  void update(Core& core);

  StateId currentStateId() const { return currentId_; }
  bool isInState(StateId id) const { return currentId_ == id; }

  // Register state instances (called during setup)
  void registerState(State* state);

 private:
  State* getState(StateId id);
  void transition(StateId next, Core& core, bool immediate);

  State* current_ = nullptr;
  StateId currentId_ = StateId::Startup;

  // State registry - pointers to pre-allocated state instances
  static constexpr size_t MAX_STATES = static_cast<size_t>(StateId::Count);
  State* states_[MAX_STATES] = {};
  size_t stateCount_ = 0;
};

}  // namespace papyrix
