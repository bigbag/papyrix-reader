#pragma once

class InputManager {
 public:
  unsigned stops = 0;
  void stopSampling() { ++stops; }
};
