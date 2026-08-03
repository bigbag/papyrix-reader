#include "ImageViewerApp.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <CoverHelpers.h>
#include <EInkDisplay.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <ImageConverter.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <SdFat.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "../core/Core.h"
#include "../ui/Elements.h"
#include "ThemeManager.h"

#define TAG "IMG_VIEWER"

extern GfxRenderer renderer;

namespace papyrix {
namespace imageviewer_app {

static constexpr const char* IMAGES_DIR = "/images";
static constexpr const char* SETTINGS_PATH = "/.papyrix/apps/image-viewer.txt";
static constexpr uint32_t SLIDESHOW_INTERVALS[] = {0, 30000, 60000, 300000};
static constexpr const char* SLIDESHOW_LABELS[] = {"Off", "30s", "60s", "5min"};
static constexpr int SLIDESHOW_COUNT = static_cast<int>(std::size(SLIDESHOW_INTERVALS));
static_assert(std::size(SLIDESHOW_INTERVALS) == std::size(SLIDESHOW_LABELS));

static constexpr int BOTTOM_BAR_HEIGHT = 23;
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

  FsFile dir = SdMan.open(IMAGES_DIR);
  if (!dir || !dir.isDirectory()) {
    LOG_INF(TAG, "No %s directory found", IMAGES_DIR);
    if (dir) dir.close();
    return;
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
      std::string path = std::string(IMAGES_DIR) + "/" + name;
      state.files.push_back(path);
    }

    entry.close();
  }
  dir.close();

  std::sort(state.files.begin(), state.files.end());

  LOG_INF(TAG, "Found %zu images", state.files.size());
}

// Convert JPEG/PNG to BMP in-place (same directory, replace original).
// Returns the new BMP path, or empty string on failure.
static std::string convertToBmpInPlace(const std::string& path) {
  std::string bmpPath = path.substr(0, path.rfind('.')) + ".bmp";

  // If target BMP already exists, just remove the original
  if (SdMan.exists(bmpPath.c_str())) {
    SdMan.remove(path.c_str());
    return bmpPath;
  }

  const int maxW = renderer.getScreenWidth();
  const int maxH = renderer.getScreenHeight() - BOTTOM_BAR_HEIGHT;

  ImageConvertConfig config;
  config.maxWidth = maxW;
  config.maxHeight = maxH;
  config.oneBit = false;
  config.logTag = TAG;

  if (!ImageConverterFactory::convertToBmp(path, bmpPath, config)) {
    return "";
  }

  SdMan.remove(path.c_str());
  LOG_INF(TAG, "Converted: %s -> %s", path.c_str(), bmpPath.c_str());
  return bmpPath;
}

static bool renderImage() {
  const Theme& theme = THEME;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  renderer.clearScreen(theme.backgroundColor);

  if (state.files.empty()) {
    ui::centeredMessage(renderer, theme, theme.uiFontId, "No images in /images/");
    ui::ButtonBar buttons("Back", "Menu", "", "");
    ui::buttonBar(renderer, theme, buttons);
    return false;
  }

  const std::string& path = state.files[state.currentIndex];

  // Convert non-BMP images to BMP in-place (replaces original file)
  if (!FsHelpers::isBmpFile(path)) {
    ui::centeredMessage(renderer, theme, theme.uiFontId, "Converting...");
    renderer.displayBuffer();

    std::string bmpPath = convertToBmpInPlace(path);
    if (bmpPath.empty()) {
      renderer.clearScreen(theme.backgroundColor);
      ui::centeredMessage(renderer, theme, theme.uiFontId, "Conversion failed");
      ui::ButtonBar buttons("Back", "Menu", "<", ">");
      ui::buttonBar(renderer, theme, buttons);
      return false;
    }

    state.files[state.currentIndex] = bmpPath;
    renderer.clearScreen(theme.backgroundColor);
  }

  const std::string& bmpPath = state.files[state.currentIndex];

  FsFile file;
  if (!SdMan.openFileForRead(TAG, bmpPath, file)) {
    LOG_ERR(TAG, "Failed to open: %s", bmpPath.c_str());
    ui::centeredMessage(renderer, theme, theme.uiFontId, "Failed to open image");
    ui::ButtonBar buttons("Back", "Menu", "<", ">");
    ui::buttonBar(renderer, theme, buttons);
    return false;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_ERR(TAG, "Invalid BMP: %s", bmpPath.c_str());
    file.close();
    ui::centeredMessage(renderer, theme, theme.uiFontId, "Invalid image file");
    ui::ButtonBar buttons("Back", "Menu", "<", ">");
    ui::buttonBar(renderer, theme, buttons);
    return false;
  }

  const int viewportH = screenH - BOTTOM_BAR_HEIGHT;
  auto rect = CoverHelpers::calculateCenteredRect(bitmap.getWidth(), bitmap.getHeight(), 0, 0, screenW, viewportH);
  renderer.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);

  ui::ButtonBar buttons("Back", "Menu", "<", ">");
  ui::buttonBar(renderer, theme, buttons);
  renderer.displayBuffer();

  if (bitmap.hasGreyscale()) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);

    bitmap.rewindToData();
    renderer.clearScreen(theme.backgroundColor);
    renderer.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
    ui::buttonBar(renderer, theme, buttons);
    renderer.cleanupGrayscaleWithFrameBuffer();
  }

  file.close();
  return true;
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

void onButton(Core& core, Button btn) {
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

bool render(Core& core) {
  (void)core;
  return renderImage();
}

void exit(Core& core) {
  (void)core;
  LOG_INF(TAG, "Image Viewer exit");
  state.files.clear();
  state.files.shrink_to_fit();
}

void renderMenu(Core& core) {
  (void)core;

  char label[32];
  snprintf(label, sizeof(label), "Slideshow: %s", SLIDESHOW_LABELS[state.slideshowSetting]);

  const char* items[] = {label};
  ui::popupMenu(renderer, THEME, "Image Viewer", items, 1, state.menuSelected);
}

void onMenuButton(Core& core, Button btn) {
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

}  // namespace imageviewer_app
}  // namespace papyrix
