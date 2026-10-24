#include "TouchTransform.h"

#include <atomic>
#if PAPYRIX_TARGET_MURPHY_M4
#include "MurphyM4Batch.h"
#endif

namespace papyrix::board {
namespace {

bool validPanelPoint(int16_t x, int16_t y, int16_t width, int16_t height) {
  return width > 0 && height > 0 && x >= 0 && y >= 0 && x < width && y < height;
}

std::atomic<DisplayOrientation> gDisplayOrientation{DisplayOrientation::Portrait};
std::atomic<uint8_t> gDisplayOrientationRevision{0};

int16_t scaleAxis(uint16_t value, uint16_t minimum, uint16_t maximum, int16_t outputSize) {
  return static_cast<int16_t>(static_cast<uint32_t>(value - minimum) * static_cast<uint32_t>(outputSize - 1) /
                              static_cast<uint32_t>(maximum - minimum));
}

}  // namespace

void setDisplayOrientation(DisplayOrientation orientation) {
  if (gDisplayOrientation == orientation) return;
  gDisplayOrientation = orientation;
  ++gDisplayOrientationRevision;
}

DisplayOrientation currentDisplayOrientation() { return gDisplayOrientation; }

uint8_t displayOrientationRevision() { return gDisplayOrientationRevision; }

void logicalSize(DisplayOrientation orientation, int16_t panelWidth, int16_t panelHeight, int16_t& logicalWidth,
                 int16_t& logicalHeight) {
  const bool portrait =
      orientation == DisplayOrientation::Portrait || orientation == DisplayOrientation::PortraitInverted;
  logicalWidth = portrait ? panelHeight : panelWidth;
  logicalHeight = portrait ? panelWidth : panelHeight;
}

bool panelFromLogical(DisplayOrientation orientation, int16_t panelWidth, int16_t panelHeight, int16_t logicalX,
                      int16_t logicalY, PanelPoint& panel) {
  int16_t logicalWidth = 0;
  int16_t logicalHeight = 0;
  logicalSize(orientation, panelWidth, panelHeight, logicalWidth, logicalHeight);
  if (!validPanelPoint(logicalX, logicalY, logicalWidth, logicalHeight)) return false;

  switch (orientation) {
    case DisplayOrientation::Portrait:
      panel = {logicalY, static_cast<int16_t>(panelHeight - 1 - logicalX)};
      break;
    case DisplayOrientation::LandscapeClockwise:
      panel = {static_cast<int16_t>(panelWidth - 1 - logicalX), static_cast<int16_t>(panelHeight - 1 - logicalY)};
      break;
    case DisplayOrientation::PortraitInverted:
      panel = {static_cast<int16_t>(panelWidth - 1 - logicalY), logicalX};
      break;
    case DisplayOrientation::LandscapeCounterClockwise:
      panel = {logicalX, logicalY};
      break;
  }
  return true;
}

bool logicalFromPanel(DisplayOrientation orientation, int16_t panelWidth, int16_t panelHeight, int16_t panelX,
                      int16_t panelY, PanelPoint& logical) {
  if (!validPanelPoint(panelX, panelY, panelWidth, panelHeight)) return false;

  switch (orientation) {
    case DisplayOrientation::Portrait:
      logical = {static_cast<int16_t>(panelHeight - 1 - panelY), panelX};
      break;
    case DisplayOrientation::LandscapeClockwise:
      logical = {static_cast<int16_t>(panelWidth - 1 - panelX), static_cast<int16_t>(panelHeight - 1 - panelY)};
      break;
    case DisplayOrientation::PortraitInverted:
      logical = {panelY, static_cast<int16_t>(panelWidth - 1 - panelX)};
      break;
    case DisplayOrientation::LandscapeCounterClockwise:
      logical = {panelX, panelY};
      break;
  }
  return true;
}

bool rawToPanel(const TouchConfig& config, uint16_t rawX, uint16_t rawY, int16_t panelWidth, int16_t panelHeight,
                PanelPoint& panel) {
  if (panelWidth <= 0 || panelHeight <= 0 || config.rawMaxX <= config.rawMinX || config.rawMaxY <= config.rawMinY) {
    return false;
  }

  const uint16_t sourceX = config.swapXY ? rawY : rawX;
  const uint16_t sourceY = config.swapXY ? rawX : rawY;
  if (sourceX < config.rawMinX || sourceX > config.rawMaxX || sourceY < config.rawMinY || sourceY > config.rawMaxY) {
    return false;
  }

  panel.x = scaleAxis(sourceX, config.rawMinX, config.rawMaxX, panelWidth);
  panel.y = scaleAxis(sourceY, config.rawMinY, config.rawMaxY, panelHeight);
#if PAPYRIX_TARGET_MURPHY_M4
  if (config.controller == TouchController::Ft6336u)
    panel.y = static_cast<int16_t>(mapMurphyM4ShortAxis(sourceY, murphyM4Batch(), panelHeight - 1));
#endif
  if (config.flipX) panel.x = static_cast<int16_t>(panelWidth - 1 - panel.x);
  if (config.flipY) panel.y = static_cast<int16_t>(panelHeight - 1 - panel.y);
  return true;
}

bool rawToLogical(const TouchConfig& config, uint16_t rawX, uint16_t rawY, int16_t panelWidth, int16_t panelHeight,
                  DisplayOrientation orientation, PanelPoint& logical) {
  PanelPoint panel{};
  return rawToPanel(config, rawX, rawY, panelWidth, panelHeight, panel) &&
         logicalFromPanel(orientation, panelWidth, panelHeight, panel.x, panel.y, logical);
}

}  // namespace papyrix::board
