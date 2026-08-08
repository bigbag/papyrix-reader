#include "test_utils.h"

#include <string>

#include "EmergencyBootTransition.h"
#include "HardwareSerial.h"

int main() {
  using namespace papyrix;
  TestUtils::TestRunner runner("EmergencyBootTransition");

  ModeTransition transition{};
  consumeEmergencyUiTransition(transition);
  runner.expectFalse(consumeEmergencyUiTransition(transition), "empty_marker_rejected");

  const ReturnTo destinations[] = {ReturnTo::HOME, ReturnTo::FILE_MANAGER, ReturnTo::RECENT};
  for (const ReturnTo destination : destinations) {
    saveEmergencyUiTransition(destination);
    runner.expectTrue(consumeEmergencyUiTransition(transition),
                      "saved_marker_consumed_" + std::to_string(static_cast<int>(destination)));
    runner.expectEq<uint32_t>(ModeTransition::MAGIC, transition.magic, "transition_magic");
    runner.expectEq<int>(static_cast<int>(BootMode::UI), static_cast<int>(transition.mode), "transition_mode_ui");
    runner.expectEq<int>(static_cast<int>(destination), static_cast<int>(transition.returnTo),
                         "transition_return_destination");
    runner.expectEq<char>('\0', transition.bookPath[0], "transition_book_path_empty");
    runner.expectFalse(consumeEmergencyUiTransition(transition), "marker_consumed_once");
  }

  saveEmergencyUiTransition(static_cast<ReturnTo>(0xFF));
  runner.expectFalse(consumeEmergencyUiTransition(transition), "invalid_destination_rejected");
  runner.expectFalse(consumeEmergencyUiTransition(transition), "invalid_destination_consumed");

  return runner.allPassed() ? 0 : 1;
}
