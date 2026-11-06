#include "ImageViewerApp.h"

#include <Arduino.h>
#include <Display.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <SdFat.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "../core/Core.h"
#include "../ui/Elements.h"
#include "../ui/ImageFileView.h"
#include "MiniAppInput.h"
#include "ThemeManager.h"

#define TAG "IMG_VIEWER"

extern GfxRenderer renderer;

namespace papyrix {
namespace imageviewer_app {

static constexpr const char* IMAGES_DIR = "/images";
static constexpr const char* PRINTOUTS_DIR = "/printouts";
static constexpr const char* SETTINGS_PATH = "/.papyrix/apps/image-viewer.txt";
static constexpr uint32_t SLIDESHOW_INTERVALS[] = {0, 30000, 60000, 300000};
static constexpr const char* SLIDESHOW_LABELS[] = {"Off", "30s", "60s", "5min"};
static constexpr int SLIDESHOW_COUNT = static_cast<int>(std::size(SLIDESHOW_INTERVALS));
static_assert(std::size(SLIDESHOW_INTERVALS) == std::size(SLIDESHOW_LABELS));

static constexpr uint32_t MAX_IMAGE_FILE_SIZE = 10 * 1024 * 1024;  // 10 MB

static struct {
  std::vector<std::string> files;
  int currentIndex = 0;
  int slideshowSetting = 0;
  unsigned long lastAdvanceMs = 0;
  int8_t menuSelected = 0;
} state;

static void loadSettings(Core& core) {
  char buf[64];
  auto result = core.storage.readToBuffer(SETTINGS_PATH, buf, sizeof(buf));
  if (!result.ok()) return;

  size_t len = result.value;
  buf[len < sizeof(buf) ? len : sizeof(buf) - 1] = '\0';

  char* line = buf;
  while (line && *line) {
    char* nl = strchr(line, '\n');
    if (nl) *nl = '\0';

    if (strncmp(line, "slideshowInterval=", 18) == 0) {
      int val = atoi(line + 18);
      if (val >= 0 && val < SLIDESHOW_COUNT) {
        state.slideshowSetting = val;
      }
    }

    line = nl ? nl + 1 : nullptr;
  }
}

static void saveSettings(Core& core) {
  core.storage.mkdir("/.papyrix/apps");

  FsFile file;
  auto result = core.storage.openWrite(SETTINGS_PATH, file);
  if (!result.ok()) return;

  char buf[64];
  snprintf(buf, sizeof(buf), "slideshowInterval=%d\n", state.slideshowSetting);
  file.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  file.close();
}

static void scanImages() {
  state.files.clear();

  // /printouts holds the printer output tray next to the user images.
  for (const char* scanDir : {IMAGES_DIR, PRINTOUTS_DIR}) {
    FsFile dir = SdMan.open(scanDir);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }

    char name[256];
    FsFile entry;
    while ((entry = dir.openNextFile())) {
      if (entry.isDirectory()) {
        entry.close();
        continue;
      }

      entry.getName(name, sizeof(name));

      if (name[0] == '.') {
        entry.close();
        continue;
      }

      if (FsHelpers::isImageFile(name) && entry.fileSize() <= MAX_IMAGE_FILE_SIZE) {
        std::string path = std::string(scanDir) + "/" + name;
        state.files.push_back(path);
      }

      entry.close();
    }
    dir.close();
  }

  std::sort(state.files.begin(), state.files.end());

  LOG_INF(TAG, "Found %zu images", state.files.size());
}

static bool renderImage() {
  const Theme& theme = THEME;
  const int viewportH = renderer.getScreenHeight() - ui::IMAGE_VIEW_BUTTON_BAR_HEIGHT;

  renderer.clearScreen(theme.backgroundColor);

  if (state.files.empty()) {
    ui::ButtonBar buttons("Back", "Menu", "", "");
    ui::drawImageError(renderer, theme, "No images in /images/", buttons);
    return false;
  }

  const std::string& path = state.files[state.currentIndex];

  // Convert non-BMP images to a hidden cache BMP (original file kept)
  if (ui::needsBmpConversion(path)) {
    ui::centeredMessage(renderer, theme, theme.uiFontId, tr(CONVERTING));
  }

  const std::string bmpPath = ui::ensureRenderableBmp(path, renderer.getScreenWidth(), viewportH, TAG);
  if (bmpPath.empty()) {
    renderer.clearScreen(theme.backgroundColor);
    ui::ButtonBar buttons("Back", "Menu", "<", ">");
    ui::drawImageError(renderer, theme, tr(CONVERSION_FAILED), buttons);
    return false;
  }

  ui::ButtonBar buttons("Back", "Menu", "<", ">");
  return ui::renderImageFile(renderer, theme, bmpPath, viewportH, buttons);
}

void enter(Core& core) {
  LOG_INF(TAG, "Image Viewer enter");
  state.currentIndex = 0;
  state.lastAdvanceMs = millis();
  state.menuSelected = 0;

  loadSettings(core);
  scanImages();
}

bool update(Core& core) {
  (void)core;

  uint32_t interval = SLIDESHOW_INTERVALS[state.slideshowSetting];
  if (interval == 0 || state.files.empty()) return false;

  unsigned long now = millis();
  if (now - state.lastAdvanceMs >= interval) {
    state.currentIndex = (state.currentIndex + 1) % static_cast<int>(state.files.size());
    state.lastAdvanceMs = now;
    return true;
  }
  return false;
}

static void onButton(Core& core, Button btn) {
  (void)core;

  if (state.files.empty()) return;

  const int count = static_cast<int>(state.files.size());

  switch (btn) {
    case Button::Left:
      state.currentIndex = (state.currentIndex - 1 + count) % count;
      state.lastAdvanceMs = millis();

      break;
    case Button::Right:
      state.currentIndex = (state.currentIndex + 1) % count;
      state.lastAdvanceMs = millis();

      break;
    default:
      break;
  }
}

static void renderMenu(Core& core);

miniapps::Frame render(Core& core, bool menuOpen) {
  if (menuOpen) {
    renderMenu(core);
    return {miniapps::Refresh::Fast, false};
  }
  return {renderImage() ? miniapps::Refresh::None : miniapps::Refresh::Fast, false};
}

void exit(Core& core) {
  (void)core;
  LOG_INF(TAG, "Image Viewer exit");
  state.files.clear();
  state.files.shrink_to_fit();
}

static void renderMenu(Core& core) {
  (void)core;
  renderer.clearScreen(THEME.backgroundColor);
  char label[32];
  snprintf(label, sizeof(label), "Slideshow: %s", SLIDESHOW_LABELS[state.slideshowSetting]);

  const char* items[] = {label};
  ui::popupMenu(renderer, THEME, "Image Viewer", items, 1, state.menuSelected);
  const int btnY = renderer.getScreenHeight() - 50;
  renderer.clearArea(0, btnY, renderer.getScreenWidth(), 50, THEME.backgroundColor);
  ui::buttonBar(renderer, THEME, ui::ButtonBar(tr(BACK), tr(CONFIRM), "<", ">"));
}

static void onMenuButton(Core& core, Button btn) {
  switch (btn) {
    case Button::Left:
    case Button::Right: {
      int delta = (btn == Button::Right) ? 1 : -1;
      state.slideshowSetting = (state.slideshowSetting + delta + SLIDESHOW_COUNT) % SLIDESHOW_COUNT;
      state.lastAdvanceMs = millis();
      saveSettings(core);

      break;
    }
    default:
      break;
  }
}

miniapps::Action handleEvent(Core& core, const Event& event, bool menuOpen) {
  return handleAppButtons(core, event, menuOpen, onButton, onMenuButton);
}

}  // namespace imageviewer_app
}  // namespace papyrix
