#pragma once

class GfxRenderer;

class ScreenComponents {
 public:
  // Draw battery icon + percentage text at specified position
  // x, y: top-left corner of the battery indicator
  static void drawBattery(const GfxRenderer& renderer, int x, int y);
};
