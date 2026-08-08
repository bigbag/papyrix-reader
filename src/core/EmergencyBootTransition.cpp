#include "EmergencyBootTransition.h"

#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif

namespace papyrix {
namespace {

constexpr uint32_t kEmergencyTransitionMagic = 0x45554954;  // "EUIT"

struct EmergencyTransitionMarker {
  uint32_t magic;
  uint8_t returnTo;
};

RTC_NOINIT_ATTR EmergencyTransitionMarker emergencyTransition;

bool isValidReturnTo(const uint8_t value) { return value <= static_cast<uint8_t>(ReturnTo::RECENT); }

}  // namespace

void saveEmergencyUiTransition(const ReturnTo returnTo) {
  emergencyTransition.returnTo = static_cast<uint8_t>(returnTo);
  emergencyTransition.magic = kEmergencyTransitionMagic;
}

bool consumeEmergencyUiTransition(ModeTransition& transition) {
  if (emergencyTransition.magic != kEmergencyTransitionMagic || !isValidReturnTo(emergencyTransition.returnTo)) {
    emergencyTransition.magic = 0;
    return false;
  }

  const ReturnTo returnTo = static_cast<ReturnTo>(emergencyTransition.returnTo);
  emergencyTransition.magic = 0;

  transition.magic = ModeTransition::MAGIC;
  transition.mode = BootMode::UI;
  transition.returnTo = returnTo;
  transition.bookPath[0] = '\0';
  return true;
}

}  // namespace papyrix
