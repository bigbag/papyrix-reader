#include "StateMachine.h"

#include <Arduino.h>
#include <Logging.h>

#include "Core.h"

#define TAG "STATE"

namespace papyrix {

void StateMachine::init(Core& core, StateId initialState) {
  // Exit current state if one exists (e.g., when triggering sleep from any state)
  if (current_) {
    current_->exit(core);
  }

  currentId_ = initialState;
  current_ = getState(initialState);

  if (current_) {
    LOG_INF(TAG, "Initial state: %d", static_cast<int>(initialState));
    current_->enter(core);
  } else {
    LOG_ERR(TAG, "No state registered for id %d", static_cast<int>(initialState));
  }
}

void StateMachine::update(Core& core) {
  if (!current_) {
    return;
  }

  StateTransition trans = current_->update(core);

  if (trans.next != currentId_) {
    transition(trans.next, core, trans.immediate);
  }

  current_->render(core);
}

void StateMachine::registerState(State* state) {
  if (!state) return;

  if (stateCount_ >= MAX_STATES) {
    LOG_ERR(TAG, "Too many states registered");
    return;
  }

  states_[stateCount_++] = state;
  LOG_DBG(TAG, "Registered state: %d", static_cast<int>(state->id()));
}

State* StateMachine::getState(StateId id) {
  for (size_t i = 0; i < stateCount_; ++i) {
    if (states_[i] && states_[i]->id() == id) {
      return states_[i];
    }
  }
  return nullptr;
}

void StateMachine::transition(StateId next, Core& core, bool immediate) {
  State* nextState = getState(next);

  if (!nextState) {
    LOG_ERR(TAG, "No state for id %d", static_cast<int>(next));
    return;
  }

  LOG_INF(TAG, "Transition: %d -> %d%s", static_cast<int>(currentId_), static_cast<int>(next),
          immediate ? " (immediate)" : "");

  if (current_) {
    current_->exit(core);
  }

  currentId_ = next;
  current_ = nextState;
  current_->enter(core);
}

}  // namespace papyrix
