#include "PomodoroApp.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <SdFat.h>

#include <cstring>

#include "../core/Core.h"
#include "../images/PomodoroBreakCat.h"
#include "../images/PomodoroFocusCat.h"
#include "../ui/Elements.h"
#include "../ui/views/SettingsViews.h"
#include "ClockFacePrimitives.h"
#include "PomodoroTimer.h"
#include "ThemeManager.h"

extern GfxRenderer renderer;

namespace papyrix::pomodoro_app {
namespace {
constexpr const char* kSettingsPath = "/.papyrix/apps/pomodoro.txt";
constexpr int kCellSize = 26;
struct State {
  pomodoro::Timer timer;
  bool settingsChanged = false;
};

State state;

void loadSettings(Core& core) {
  char buf[64];
  auto result = core.storage.readToBuffer(kSettingsPath, buf, sizeof(buf));
  if (!result.ok()) return;
  buf[result.value < sizeof(buf) ? result.value : sizeof(buf) - 1] = '\0';
  char* line = buf;
  while (line && *line) {
    char* nl = std::strchr(line, '\n');
    if (nl) *nl = '\0';
    if (std::strcmp(line, "startMode=manual") == 0) {
      state.timer.startMode = pomodoro::StartMode::Manual;
    } else if (std::strcmp(line, "startMode=auto") == 0) {
      state.timer.startMode = pomodoro::StartMode::Auto;
    }
    line = nl ? nl + 1 : nullptr;
  }
}

void saveSettings(Core& core) {
  if (!state.settingsChanged) return;
  core.storage.mkdir("/.papyrix/apps");
  FsFile file;
  if (!core.storage.openWrite(kSettingsPath, file).ok()) return;
  const char* text = state.timer.startMode == pomodoro::StartMode::Auto ? "startMode=auto\n" : "startMode=manual\n";
  file.write(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
  file.close();
  state.settingsChanged = false;
}

void drawMinutes(const GfxRenderer& renderer, int centerX, int y, uint32_t minutes, bool color) {
  const int value = minutes > 99u ? 99 : static_cast<int>(minutes);
  int x = centerX - (11 * kCellSize) / 2;
  clock_face_primitives::drawBlockDigit(renderer, x, y, value / 10, kCellSize, color);
  clock_face_primitives::drawBlockDigit(renderer, x + 6 * kCellSize, y, value % 10, kCellSize, color);
}
}  // namespace
void enter(Core& core) {
  state = State{};
  loadSettings(core);
}

bool update(Core& core) {
  const auto runBefore = state.timer.run;
  const auto periodBefore = state.timer.period;
  const uint32_t minutesBefore = pomodoro::displayedMinutes(state.timer.remainingMs);
  pomodoro::tick(state.timer, millis());
  const bool changed = state.timer.run != runBefore || state.timer.period != periodBefore ||
                       pomodoro::displayedMinutes(state.timer.remainingMs) != minutesBefore;
  if (changed)
    core.cpu.unthrottle();
  else
    core.cpu.throttle();
  return changed;
}

void onButton(Core& core, Button btn) {
  const uint32_t now = millis();
  if (btn == Button::Left) {
    if (state.timer.run == pomodoro::RunState::Running && state.timer.period != pomodoro::Period::Focus) {
      pomodoro::skipBreak(state.timer, now);
    } else if (state.timer.run == pomodoro::RunState::Paused) {
      pomodoro::resume(state.timer, now);
    } else if (state.timer.run == pomodoro::RunState::Running) {
      pomodoro::pause(state.timer, now);
    } else {
      pomodoro::start(state.timer, now);
    }
  } else if (btn == Button::Right) {
    pomodoro::reset(state.timer);
  }
  (void)core;
}

bool render(Core& core) {
  (void)core;
  const Theme& theme = THEME;
  renderer.clearScreen(theme.backgroundColor);
  const auto layout = clock_face_primitives::makeLayout(renderer);
  char label[32];
  pomodoro::writePeriodLabel(state.timer, label, sizeof(label));
  renderer.drawCenteredText(theme.uiFontId, layout.top + 12, label, theme.secondaryTextBlack);

  constexpr int digitHeight = 7 * kCellSize;
  constexpr int catSize = PomodoroFocusCatSize;
  const int smallLineHeight = renderer.getLineHeight(theme.smallFontId);
  const int groupHeight = digitHeight + 8 + smallLineHeight + 24 + catSize + 12 + smallLineHeight;
  const int digitY = layout.top + (layout.contentHeight - groupHeight) / 2;
  const uint32_t minutes = pomodoro::displayedMinutes(state.timer.remainingMs);
  drawMinutes(renderer, layout.centerX, digitY, minutes, theme.primaryTextBlack);
  const int unitY = digitY + digitHeight + 8;
  renderer.drawCenteredText(theme.smallFontId, unitY, "minutes left", theme.secondaryTextBlack);

  const int catY = unitY + smallLineHeight + 24;
  const uint8_t* cat = state.timer.period == pomodoro::Period::Focus ? PomodoroFocusCat : PomodoroBreakCat;
  ui::inkBitmap(renderer, cat, catSize, layout.centerX - catSize / 2, catY, theme.primaryTextBlack);

  renderer.drawCenteredText(theme.smallFontId, catY + catSize + 12, pomodoro::statusLabel(state.timer),
                            theme.secondaryTextBlack);
  ui::ButtonBar buttons("Back", "Menu", pomodoro::thirdButtonLabel(state.timer), "Reset");
  ui::buttonBar(renderer, theme, buttons);
  return false;
}

void exit(Core& core) {
  core.cpu.unthrottle();
  saveSettings(core);
}

void renderMenu(Core&) {
  const Theme& theme = THEME;
  ui::title(renderer, theme, theme.screenMarginTop, "Pomodoro");
  ui::enumValue(renderer, theme, ui::SettingsListHit::LIST_START_Y, "Start mode",
                state.timer.startMode == pomodoro::StartMode::Auto ? "Auto" : "Manual", true);
  ui::ButtonBar buttons("Back", "", "<", ">");
  ui::buttonBar(renderer, theme, buttons);
}

void onMenuButton(Core& core, Button btn) {
  if (btn == Button::Left || btn == Button::Right) {
    state.timer.startMode =
        state.timer.startMode == pomodoro::StartMode::Auto ? pomodoro::StartMode::Manual : pomodoro::StartMode::Auto;
    state.settingsChanged = true;
  }
  if (btn == Button::Back) saveSettings(core);
}
}  // namespace papyrix::pomodoro_app
