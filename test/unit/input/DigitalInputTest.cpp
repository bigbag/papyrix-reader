#include <Arduino.h>
#include <InputManager.h>
#include <TargetConfig.h>

#include <cassert>

int main() {
  InputManager input;
  testResetGpioEvents();
  input.begin();
  for (size_t i = 0; i < testGpioEventCount; ++i) {
    assert(testGpioEvents[i].pin != 1);
#if PAPYRIX_TARGET_X4PRO
    assert(testGpioEvents[i].pin != 2);
#endif
  }
  testSetDigitalReadHook([](int pin) { return pin == (PAPYRIX_TARGET_STICKY ? 6 : PAPYRIX_TARGET_X4CLASSIC ? 0 : 7) ? LOW : HIGH; });
  assert(input.getState() == (1 << InputManager::BTN_DOWN));
  testSetDigitalReadHook([](int pin) { return pin == (PAPYRIX_TARGET_STICKY ? 5 : PAPYRIX_TARGET_X4CLASSIC ? 7 : 0) ? LOW : HIGH; });
  assert(input.getState() == (1 << InputManager::BTN_UP));
  testSetDigitalReadHook([](int pin) { return pin == (PAPYRIX_TARGET_STICKY ? 4 : 3) ? LOW : HIGH; });
  assert(input.getState() == (1 << InputManager::BTN_POWER));
#if PAPYRIX_TARGET_X4CLASSIC
  testSetDigitalReadHook([](int pin) { return pin == 9 ? LOW : HIGH; });
  assert(input.getState() == (1 << InputManager::BTN_BACK));
  testSetDigitalReadHook([](int pin) { return pin == 8 ? LOW : HIGH; });
  assert(input.getState() == (1 << InputManager::BTN_CONFIRM));
  testSetDigitalReadHook([](int pin) { return pin == 5 ? LOW : HIGH; });
  assert(input.getState() == (1 << InputManager::BTN_LEFT));
  testSetDigitalReadHook([](int pin) { return pin == 2 ? LOW : HIGH; });
  assert(input.getState() == (1 << InputManager::BTN_RIGHT));
#endif
  testSetDigitalReadHook([](int) { return HIGH; });
  assert(input.getState() == 0);
  assert(testAnalogReadCount == 0);
}
