#pragma once

#include <cstdint>
#include <ctime>

class GfxRenderer;
struct Theme;

namespace papyrix::clock_faces {

enum class Face : uint8_t {
  Big = 0,
  Classic = 1,
  Analog = 2,
  Retro = 3,
  Flip = 4,
  Minimal = 5,
  Serif = 6,
  DayNight = 7,
  Count = 8,
};

inline constexpr Face SELECTABLE_FACES[] = {Face::Big, Face::Analog, Face::Retro, Face::Flip, Face::DayNight};

struct Context {
  GfxRenderer& renderer;
  const Theme& theme;
  const std::tm& time;
  bool use24h;
  int8_t dateFormat;
  int8_t utcOffset;
};

inline constexpr bool isSelectable(Face face) {
  for (Face candidate : SELECTABLE_FACES) {
    if (candidate == face) return true;
  }
  return false;
}
Face selectRelative(Face face, int delta);
const char* name(Face face);
void render(const Context& context, Face face);

}  // namespace papyrix::clock_faces
