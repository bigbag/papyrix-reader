#include "ClockFaces.h"

#include <GfxRenderer.h>
#include <Theme.h>

#include <cmath>

#include "ClockFacePrimitives.h"

namespace papyrix::clock_faces {
namespace {

using namespace clock_face_primitives;

static constexpr int16_t CLOCK_X[60] = {
    0,     107,   213,   316,  416,  512,  602,  685,  761,  828,  887,  935,  974,  1002,  1018,
    1024,  1018,  1002,  974,  935,  887,  828,  761,  685,  602,  512,  416,  316,  213,   107,
    0,     -107,  -213,  -316, -416, -512, -602, -685, -761, -828, -887, -935, -974, -1002, -1018,
    -1024, -1018, -1002, -974, -935, -887, -828, -761, -685, -602, -512, -416, -316, -213,  -107,
};
static constexpr int16_t CLOCK_Y[60] = {
    -1024, -1018, -1002, -974, -935, -887, -828, -761, -685, -602, -512, -416, -316, -213,  -107,
    0,     107,   213,   316,  416,  512,  602,  685,  761,  828,  887,  935,  974,  1002,  1018,
    1024,  1018,  1002,  974,  935,  887,  828,  761,  685,  602,  512,  416,  316,  213,   107,
    0,     -107,  -213,  -316, -416, -512, -602, -685, -761, -828, -887, -935, -974, -1002, -1018,
};

int dateY(const Layout& layout) { return layout.top + 12; }

int centeredY(const Layout& layout, int height) { return layout.top + (layout.contentHeight - height) / 2; }

void drawDate(const Context& context, const Layout& layout) {
  char value[24];
  formatDate(value, sizeof(value), context.time, context.dateFormat);
  context.renderer.drawCenteredText(context.theme.uiFontId, dateY(layout), value, context.theme.secondaryTextBlack);
}

int blockNumberWidth(int value, bool leadingZero, int cellSize) {
  return !leadingZero && value < 10 ? 5 * cellSize : 11 * cellSize;
}

void drawBlockNumber(const GfxRenderer& renderer, int centerX, int y, int value, bool leadingZero, int cellSize,
                     bool color) {
  const bool oneDigit = !leadingZero && value < 10;
  int x = centerX - blockNumberWidth(value, leadingZero, cellSize) / 2;
  if (!oneDigit) {
    drawBlockDigit(renderer, x, y, value / 10, cellSize, color);
    x += 6 * cellSize;
  }
  drawBlockDigit(renderer, x, y, value % 10, cellSize, color);
}

void drawPeriod(const Context& context, int y, const DisplayTime& value) {
  if (!context.use24h) {
    context.renderer.drawCenteredText(context.theme.smallFontId, y, value.period, context.theme.secondaryTextBlack);
  }
}

void drawBig(const Context& context) {
  constexpr int cellSize = 26;
  constexpr int digitHeight = 7 * cellSize;
  constexpr int rowGap = 26;
  const Layout layout = makeLayout(context.renderer);
  const DisplayTime value = displayTime(context.time, context.use24h);
  const int periodHeight = context.use24h ? 0 : 8 + context.renderer.getLineHeight(context.theme.smallFontId);
  const int contentHeight = 2 * digitHeight + rowGap + periodHeight;
  const int y = centeredY(layout, contentHeight);

  drawBlockNumber(context.renderer, layout.centerX, y, value.hour, value.showLeadingZero, cellSize,
                  context.theme.primaryTextBlack);
  const int minuteY = y + digitHeight + rowGap;
  drawBlockNumber(context.renderer, layout.centerX, minuteY, value.minute, true, cellSize,
                  context.theme.primaryTextBlack);
  drawPeriod(context, minuteY + digitHeight + 8, value);
  drawDate(context, layout);
}

void pointAt(int centerX, int centerY, int index, int radius, int& x, int& y) {
  x = centerX + CLOCK_X[index] * radius / 1024;
  y = centerY + CLOCK_Y[index] * radius / 1024;
}

void drawAnalog(const Context& context) {
  const Layout layout = makeLayout(context.renderer);
  const int centerY = centeredY(layout, 0);
  const int radius = layout.width >= 520 ? 183 : 175;
  drawCircle(context.renderer, layout.centerX, centerY, radius, 3, context.theme.primaryTextBlack);

  for (int index = 0; index < 60; index += 5) {
    int outerX;
    int outerY;
    int innerX;
    int innerY;
    pointAt(layout.centerX, centerY, index, radius - 8, outerX, outerY);
    pointAt(layout.centerX, centerY, index, radius - 22, innerX, innerY);
    drawThickLine(context.renderer, innerX, innerY, outerX, outerY, 3, context.theme.primaryTextBlack);
  }

  int minuteX;
  int minuteY;
  pointAt(layout.centerX, centerY, context.time.tm_min, 120, minuteX, minuteY);
  drawThickLine(context.renderer, layout.centerX, centerY, minuteX, minuteY, 5, context.theme.primaryTextBlack);

  const int hourIndex = ((context.time.tm_hour % 12) * 5 + context.time.tm_min / 12) % 60;
  int hourX;
  int hourY;
  pointAt(layout.centerX, centerY, hourIndex, 82, hourX, hourY);
  drawThickLine(context.renderer, layout.centerX, centerY, hourX, hourY, 7, context.theme.primaryTextBlack);
  fillCircle(context.renderer, layout.centerX, centerY, 7, context.theme.primaryTextBlack);
  drawDate(context, layout);
}

void drawRetroColon(const GfxRenderer& renderer, int x, int y, int digitHeight, int thickness, bool color) {
  const int dotX = x + (30 - thickness) / 2;
  renderer.fillRect(dotX, y + digitHeight / 3 - thickness / 2, thickness, thickness, color);
  renderer.fillRect(dotX, y + 2 * digitHeight / 3 - thickness / 2, thickness, thickness, color);
}

void drawRetro(const Context& context) {
  constexpr int digitWidth = 80;
  constexpr int digitHeight = 160;
  constexpr int thickness = 14;
  constexpr int digitGap = 12;
  constexpr int colonWidth = 30;
  const Layout layout = makeLayout(context.renderer);
  const DisplayTime value = displayTime(context.time, context.use24h);
  const bool skipLeading = !value.showLeadingZero && value.hour < 10;
  const int width = skipLeading ? 3 * digitWidth + digitGap + colonWidth : 4 * digitWidth + 2 * digitGap + colonWidth;
  const int periodHeight = context.use24h ? 0 : 8 + context.renderer.getLineHeight(context.theme.smallFontId);
  const int y = centeredY(layout, digitHeight + periodHeight);
  int x = layout.centerX - width / 2;

  if (!skipLeading) {
    drawSevenSegmentDigit(context.renderer, x, y, value.hour / 10, digitWidth, digitHeight, thickness,
                          context.theme.primaryTextBlack);
    x += digitWidth + digitGap;
  }
  drawSevenSegmentDigit(context.renderer, x, y, value.hour % 10, digitWidth, digitHeight, thickness,
                        context.theme.primaryTextBlack);
  x += digitWidth;
  drawRetroColon(context.renderer, x, y, digitHeight, thickness, context.theme.primaryTextBlack);
  x += colonWidth;
  drawSevenSegmentDigit(context.renderer, x, y, value.minute / 10, digitWidth, digitHeight, thickness,
                        context.theme.primaryTextBlack);
  x += digitWidth + digitGap;
  drawSevenSegmentDigit(context.renderer, x, y, value.minute % 10, digitWidth, digitHeight, thickness,
                        context.theme.primaryTextBlack);
  drawPeriod(context, y + digitHeight + 8, value);
  drawDate(context, layout);
}

void drawFlip(const Context& context) {
  constexpr int cardWidth = 286;
  constexpr int cardHeight = 150;
  constexpr int cardGap = 14;
  constexpr int cellSize = 16;
  const Layout layout = makeLayout(context.renderer);
  const DisplayTime value = displayTime(context.time, context.use24h);
  const bool cardColor = context.theme.primaryTextBlack;
  const bool digitColor = !cardColor;
  const bool backgroundBlack = context.theme.backgroundColor == 0x00;
  const int periodHeight = context.use24h ? 0 : 8 + context.renderer.getLineHeight(context.theme.smallFontId);
  const int groupHeight = 2 * cardHeight + cardGap + periodHeight;
  const int x = layout.centerX - cardWidth / 2;
  const int y = centeredY(layout, groupHeight);

  context.renderer.fillRect(x, y, cardWidth, cardHeight, cardColor);
  context.renderer.drawLine(x, y + cardHeight / 2, x + cardWidth - 1, y + cardHeight / 2, backgroundBlack);
  drawBlockNumber(context.renderer, layout.centerX, y + (cardHeight - 7 * cellSize) / 2, value.hour,
                  value.showLeadingZero, cellSize, digitColor);

  const int minuteY = y + cardHeight + cardGap;
  context.renderer.fillRect(x, minuteY, cardWidth, cardHeight, cardColor);
  context.renderer.drawLine(x, minuteY + cardHeight / 2, x + cardWidth - 1, minuteY + cardHeight / 2, backgroundBlack);
  drawBlockNumber(context.renderer, layout.centerX, minuteY + (cardHeight - 7 * cellSize) / 2, value.minute, true,
                  cellSize, digitColor);
  drawPeriod(context, minuteY + cardHeight + 8, value);
  drawDate(context, layout);
}

int blockTimeWidth(const DisplayTime& value, int cellSize) {
  return (!value.showLeadingZero && value.hour < 10 ? 18 : 24) * cellSize;
}

void drawBlockTime(const Context& context, const Layout& layout, const DisplayTime& value, int y, int cellSize) {
  const bool skipLeading = !value.showLeadingZero && value.hour < 10;
  int x = layout.centerX - blockTimeWidth(value, cellSize) / 2;
  if (!skipLeading) {
    drawBlockDigit(context.renderer, x, y, value.hour / 10, cellSize, context.theme.primaryTextBlack);
    x += 6 * cellSize;
  }
  drawBlockDigit(context.renderer, x, y, value.hour % 10, cellSize, context.theme.primaryTextBlack);
  x += 5 * cellSize;
  fillCircle(context.renderer, x + cellSize, y + 2 * cellSize, cellSize / 3, context.theme.primaryTextBlack);
  fillCircle(context.renderer, x + cellSize, y + 5 * cellSize, cellSize / 3, context.theme.primaryTextBlack);
  x += 2 * cellSize;
  drawBlockDigit(context.renderer, x, y, value.minute / 10, cellSize, context.theme.primaryTextBlack);
  x += 6 * cellSize;
  drawBlockDigit(context.renderer, x, y, value.minute % 10, cellSize, context.theme.primaryTextBlack);
}
enum class LunarPhase : uint8_t {
  New,
  WaxingCrescent,
  FirstQuarter,
  WaxingGibbous,
  Full,
  WaningGibbous,
  LastQuarter,
  WaningCrescent,
};

static constexpr const char* LUNAR_PHASE_NAMES[] = {
    "New Moon",  "Waxing Crescent", "First Quarter", "Waxing Gibbous",
    "Full Moon", "Waning Gibbous",  "Last Quarter",  "Waning Crescent",
};

int64_t gregorianDay(const std::tm& time) {
  const int month = time.tm_mon + 1;
  const int adjustment = (14 - month) / 12;
  const int year = time.tm_year + 1900 + 4800 - adjustment;
  const int adjustedMonth = month + 12 * adjustment - 3;
  return time.tm_mday + (153 * adjustedMonth + 2) / 5 + 365 * year + year / 4 - year / 100 + year / 400 - 32045;
}

float normalizeDegrees(float angle) { return angle - 360.0f * std::floor(angle / 360.0f); }

float sinDegrees(float angle) { return std::sin(angle * 0.01745329252f); }

LunarPhase lunarPhase(const std::tm& time, int8_t utcOffset) {
  constexpr int32_t epochHour = 2444239 * 24;
  static int32_t cachedUtcHour = -1;
  static LunarPhase cachedPhase = LunarPhase::New;
  const int32_t utcHour = static_cast<int32_t>(gregorianDay(time) * 24 + time.tm_hour - utcOffset);
  if (utcHour == cachedUtcHour) return cachedPhase;

  cachedUtcHour = utcHour;
  const float days = static_cast<float>(utcHour - epochHour) / 24.0f + 1.0f / 48.0f;
  const float sunMeanLongitude = normalizeDegrees((360.0f / 365.2422f) * days);
  const float sunMeanAnomaly = normalizeDegrees(sunMeanLongitude + 278.833540f - 282.596403f);
  const float sunAnomalySin = sinDegrees(sunMeanAnomaly);
  const float sunLongitude =
      normalizeDegrees(sunMeanAnomaly + 1.9148f * sunAnomalySin + 0.0200f * sinDegrees(2 * sunMeanAnomaly) +
                       0.0003f * sinDegrees(3 * sunMeanAnomaly) + 282.596403f);
  const float moonMeanLongitude = normalizeDegrees(13.1763966f * days + 64.975464f);
  const float moonMeanAnomaly = normalizeDegrees(moonMeanLongitude - 0.1114041f * days - 349.383063f);
  const float evection = 1.2739f * sinDegrees(2 * (moonMeanLongitude - sunLongitude) - moonMeanAnomaly);
  const float annualEquation = 0.1858f * sunAnomalySin;
  const float correctedMoonAnomaly = moonMeanAnomaly + evection - annualEquation - 0.37f * sunAnomalySin;
  const float correctedMoonLongitude = moonMeanLongitude + evection + 6.2886f * sinDegrees(correctedMoonAnomaly) -
                                       annualEquation + 0.214f * sinDegrees(2 * correctedMoonAnomaly);
  const float phase =
      normalizeDegrees(correctedMoonLongitude + 0.6583f * sinDegrees(2 * (correctedMoonLongitude - sunLongitude)) -
                       sunLongitude) /
      360.0f;
  const int slot = static_cast<int>(phase * 16.0f + 0.5f) % 16;
  cachedPhase = slot % 4 == 0 ? static_cast<LunarPhase>(slot / 2) : static_cast<LunarPhase>((slot / 4) * 2 + 1);
  return cachedPhase;
}

void drawLunarPhase(const Context& context, const Layout& layout, int centerY, LunarPhase phase) {
  constexpr int radius = 54;
  const int centerX = layout.centerX;
  const int radiusSquared = radius * radius;
  int halfWidth = radius;

  for (int deltaY = 0; deltaY <= radius; ++deltaY) {
    while (halfWidth > 0 && halfWidth * halfWidth + deltaY * deltaY > radiusSquared) {
      --halfWidth;
    }

    int rowX = centerX;
    int rowWidth = 0;
    switch (phase) {
      case LunarPhase::New:
        break;
      case LunarPhase::WaxingCrescent:
        rowX = centerX + halfWidth / 2;
        rowWidth = halfWidth - halfWidth / 2 + 1;
        break;
      case LunarPhase::FirstQuarter:
        rowWidth = halfWidth + 1;
        break;
      case LunarPhase::WaxingGibbous:
        rowX = centerX - halfWidth / 2;
        rowWidth = halfWidth + halfWidth / 2 + 1;
        break;
      case LunarPhase::Full:
        rowX = centerX - halfWidth;
        rowWidth = 2 * halfWidth + 1;
        break;
      case LunarPhase::WaningGibbous:
        rowX = centerX - halfWidth;
        rowWidth = halfWidth + halfWidth / 2 + 1;
        break;
      case LunarPhase::LastQuarter:
        rowX = centerX - halfWidth;
        rowWidth = halfWidth + 1;
        break;
      case LunarPhase::WaningCrescent:
        rowX = centerX - halfWidth;
        rowWidth = halfWidth - halfWidth / 2 + 1;
        break;
    }
    if (rowWidth == 0) continue;

    context.renderer.fillRect(rowX, centerY + deltaY, rowWidth, 1, context.theme.primaryTextBlack);
    if (deltaY != 0) {
      context.renderer.fillRect(rowX, centerY - deltaY, rowWidth, 1, context.theme.primaryTextBlack);
    }
  }
  drawCircle(context.renderer, centerX, centerY, radius, 5, context.theme.primaryTextBlack);
}

void drawDayNight(const Context& context) {
  constexpr int iconRadius = 70;
  constexpr int cellSize = 12;
  constexpr int timeHeight = 7 * cellSize;
  const Layout layout = makeLayout(context.renderer);
  const DisplayTime value = displayTime(context.time, context.use24h);
  const int periodHeight = context.use24h ? 0 : 8 + context.renderer.getLineHeight(context.theme.smallFontId);
  const int labelHeight = context.renderer.getLineHeight(context.theme.uiFontId);
  const int groupHeight = timeHeight + periodHeight + 24 + 2 * iconRadius + 12 + labelHeight;
  const int timeY = centeredY(layout, groupHeight);
  const int iconY = timeY + timeHeight + periodHeight + 24 + iconRadius;
  const int labelY = iconY + iconRadius + 12;

  drawBlockTime(context, layout, value, timeY, cellSize);
  drawPeriod(context, timeY + timeHeight + 8, value);
  if (context.time.tm_hour >= 6 && context.time.tm_hour < 18) {
    fillCircle(context.renderer, layout.centerX, iconY, 42, context.theme.primaryTextBlack);
    drawThickLine(context.renderer, layout.centerX, iconY - 68, layout.centerX, iconY - 54, 3,
                  context.theme.primaryTextBlack);
    drawThickLine(context.renderer, layout.centerX, iconY + 54, layout.centerX, iconY + 68, 3,
                  context.theme.primaryTextBlack);
    drawThickLine(context.renderer, layout.centerX - 68, iconY, layout.centerX - 54, iconY, 3,
                  context.theme.primaryTextBlack);
    drawThickLine(context.renderer, layout.centerX + 54, iconY, layout.centerX + 68, iconY, 3,
                  context.theme.primaryTextBlack);
    drawThickLine(context.renderer, layout.centerX - 48, iconY - 48, layout.centerX - 38, iconY - 38, 3,
                  context.theme.primaryTextBlack);
    drawThickLine(context.renderer, layout.centerX + 38, iconY - 38, layout.centerX + 48, iconY - 48, 3,
                  context.theme.primaryTextBlack);
    drawThickLine(context.renderer, layout.centerX - 48, iconY + 48, layout.centerX - 38, iconY + 38, 3,
                  context.theme.primaryTextBlack);
    drawThickLine(context.renderer, layout.centerX + 38, iconY + 38, layout.centerX + 48, iconY + 48, 3,
                  context.theme.primaryTextBlack);
  } else {
    const LunarPhase phase = lunarPhase(context.time, context.utcOffset);
    drawLunarPhase(context, layout, iconY, phase);
    context.renderer.drawCenteredText(context.theme.uiFontId, labelY, LUNAR_PHASE_NAMES[static_cast<uint8_t>(phase)],
                                      context.theme.secondaryTextBlack);
  }
  drawDate(context, layout);
}

}  // namespace

Face selectRelative(Face face, int delta) {
  constexpr int count = sizeof(SELECTABLE_FACES) / sizeof(SELECTABLE_FACES[0]);
  int index = -1;
  for (int candidate = 0; candidate < count; ++candidate) {
    if (SELECTABLE_FACES[candidate] == face) {
      index = candidate;
      break;
    }
  }
  if (index < 0) return Face::Big;

  index = (index + delta % count + count) % count;
  return SELECTABLE_FACES[index];
}

const char* name(Face face) {
  switch (face) {
    case Face::Big:
      return "Big";
    case Face::Analog:
      return "Analog";
    case Face::Retro:
      return "Retro";
    case Face::Flip:
      return "Flip";
    case Face::DayNight:
      return "Day & Night";
    default:
      return "";
  }
}

void render(const Context& context, Face face) {
  switch (face) {
    case Face::Analog:
      drawAnalog(context);
      break;
    case Face::Retro:
      drawRetro(context);
      break;
    case Face::Flip:
      drawFlip(context);
      break;
    case Face::DayNight:
      drawDayNight(context);
      break;
    default:
      drawBig(context);
      break;
  }
}

}  // namespace papyrix::clock_faces
