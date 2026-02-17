#include "FileListState.h"

#include <Arduino.h>
#include <EInkDisplay.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <esp_system.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "../core/BootMode.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"

namespace papyrix {

FileListState::FileListState(GfxRenderer& renderer)
    : renderer_(renderer),
      selectedIndex_(0),
      needsRender_(true),
      hasSelection_(false),
      goHome_(false),
      firstRender_(true),
      currentScreen_(Screen::Browse),
      confirmView_{} {
  strcpy(currentDir_, "/");
  selectedPath_[0] = '\0';
}

FileListState::~FileListState() = default;

void FileListState::setDirectory(const char* dir) {
  if (dir && dir[0] != '\0') {
    strncpy(currentDir_, dir, sizeof(currentDir_) - 1);
    currentDir_[sizeof(currentDir_) - 1] = '\0';
  } else {
    strcpy(currentDir_, "/");
  }
}

void FileListState::enter(Core& core) {
  Serial.printf("[FILES] Entering, dir: %s\n", currentDir_);

  // Preserve position when returning from Reader via boot transition
  const auto& transition = getTransition();
  bool preservePosition = transition.isValid() && transition.returnTo == ReturnTo::FILE_MANAGER;

  if (preservePosition) {
    // Restore directory from settings
    strncpy(currentDir_, core.settings.fileListDir, sizeof(currentDir_) - 1);
    currentDir_[sizeof(currentDir_) - 1] = '\0';
  }

  needsRender_ = true;
  hasSelection_ = false;
  goHome_ = false;
  firstRender_ = true;
  currentScreen_ = Screen::Browse;
  selectedPath_[0] = '\0';

  loadFiles(core);

  if (preservePosition && !files_.empty()) {
    selectedIndex_ = core.settings.fileListSelectedIndex;

    // Clamp to valid range
    if (selectedIndex_ >= files_.size()) {
      selectedIndex_ = files_.size() - 1;
    }

    // Verify filename matches, search if not
    if (strcasecmp(files_[selectedIndex_].name.c_str(), core.settings.fileListSelectedName) != 0) {
      for (size_t i = 0; i < files_.size(); i++) {
        if (strcasecmp(files_[i].name.c_str(), core.settings.fileListSelectedName) == 0) {
          selectedIndex_ = i;
          break;
        }
      }
    }
  } else {
    selectedIndex_ = 0;
  }
}

void FileListState::exit(Core& core) { Serial.println("[FILES] Exiting"); }

void FileListState::loadFiles(Core& core) {
  files_.clear();
  files_.reserve(512);  // Pre-allocate for large libraries

  FsFile dir;
  auto result = core.storage.openDir(currentDir_, dir);
  if (!result.ok()) {
    Serial.printf("[FILES] Failed to open dir: %s\n", currentDir_);
    return;
  }

  char name[256];
  FsFile entry;

  // Collect all entries (no hard limit during collection)
  while ((entry = dir.openNextFile())) {
    entry.getName(name, sizeof(name));

    if (isHidden(name)) {
      entry.close();
      continue;
    }

    bool isDir = entry.isDirectory();
    entry.close();

    if (isDir || isSupportedFile(name)) {
      files_.push_back({std::string(name), isDir});
    }
  }
  dir.close();

  // Safety check - prevent OOM on extreme cases
  constexpr size_t MAX_ENTRIES = 1000;
  if (files_.size() > MAX_ENTRIES) {
    Serial.printf("[FILES] Warning: truncated to %zu entries\n", MAX_ENTRIES);
    files_.resize(MAX_ENTRIES);
    files_.shrink_to_fit();
  }

  // Sort: directories first, then natural sort (case-insensitive)
  std::sort(files_.begin(), files_.end(), [](const FileEntry& a, const FileEntry& b) {
    if (a.isDir && !b.isDir) return true;
    if (!a.isDir && b.isDir) return false;

    const char* s1 = a.name.c_str();
    const char* s2 = b.name.c_str();

    while (*s1 && *s2) {
      const auto uc = [](char c) { return static_cast<unsigned char>(c); };
      if (std::isdigit(uc(*s1)) && std::isdigit(uc(*s2))) {
        // Skip leading zeros
        while (*s1 == '0') s1++;
        while (*s2 == '0') s2++;

        // Compare by digit length first
        int len1 = 0, len2 = 0;
        while (std::isdigit(uc(s1[len1]))) len1++;
        while (std::isdigit(uc(s2[len2]))) len2++;
        if (len1 != len2) return len1 < len2;

        // Same length: compare digit by digit
        for (int i = 0; i < len1; i++) {
          if (s1[i] != s2[i]) return s1[i] < s2[i];
        }
        s1 += len1;
        s2 += len2;
      } else {
        char c1 = std::tolower(uc(*s1));
        char c2 = std::tolower(uc(*s2));
        if (c1 != c2) return c1 < c2;
        s1++;
        s2++;
      }
    }
    return *s1 == '\0' && *s2 != '\0';
  });

  Serial.printf("[FILES] Loaded %zu entries\n", files_.size());
}

bool FileListState::isHidden(const char* name) const {
  if (name[0] == '.') return true;
  if (FsHelpers::isHiddenFsItem(name)) return true;
  if (strncmp(name, "FOUND.", 6) == 0) return true;
  return false;
}

bool FileListState::isSupportedFile(const char* name) const {
  const char* ext = strrchr(name, '.');
  if (!ext) return false;
  ext++;  // Skip the dot

  // Case-insensitive extension check (matches ContentTypes.cpp)
  if (strcasecmp(ext, "epub") == 0) return true;
  if (strcasecmp(ext, "xtc") == 0) return true;
  if (strcasecmp(ext, "xtch") == 0) return true;
  if (strcasecmp(ext, "xtg") == 0) return true;
  if (strcasecmp(ext, "xth") == 0) return true;
  if (strcasecmp(ext, "txt") == 0) return true;
  if (strcasecmp(ext, "md") == 0) return true;
  if (strcasecmp(ext, "markdown") == 0) return true;
  if (strcasecmp(ext, "fb2") == 0) return true;
  return false;
}

StateTransition FileListState::update(Core& core) {
  // Process input events
  Event e;
  while (core.events.pop(e)) {
    switch (e.type) {
      case EventType::ButtonPress:
        if (currentScreen_ == Screen::ConfirmDelete) {
          // Confirmation dialog input
          switch (e.button) {
            case Button::Up:
            case Button::Down:
              confirmView_.toggleSelection();
              needsRender_ = true;
              break;
            case Button::Center:
              if (confirmView_.isYesSelected()) {
                // Execute delete inline (like SettingsState pattern)
                const FileEntry& entry = files_[selectedIndex_];
                char pathBuf[512];  // currentDir_(256) + '/' + name(128)
                size_t dirLen = strlen(currentDir_);
                if (currentDir_[dirLen - 1] == '/') {
                  snprintf(pathBuf, sizeof(pathBuf), "%s%s", currentDir_, entry.name.c_str());
                } else {
                  snprintf(pathBuf, sizeof(pathBuf), "%s/%s", currentDir_, entry.name.c_str());
                }

                // Check if trying to delete the currently active book
                const char* activeBook = core.settings.lastBookPath;
                if (activeBook[0] != '\0' && strcmp(pathBuf, activeBook) == 0) {
                  ui::centeredMessage(renderer_, THEME, THEME.uiFontId, "Cannot delete active book");
                  vTaskDelay(1500 / portTICK_PERIOD_MS);
                } else {
                  ui::centeredMessage(renderer_, THEME, THEME.uiFontId, "Deleting...");

                  Result<void> result = entry.isDir ? core.storage.rmdir(pathBuf) : core.storage.remove(pathBuf);

                  const char* msg = result.ok() ? "Deleted" : "Delete failed";
                  ui::centeredMessage(renderer_, THEME, THEME.uiFontId, msg);
                  vTaskDelay(1000 / portTICK_PERIOD_MS);

                  loadFiles(core);
                  if (selectedIndex_ >= files_.size()) {
                    selectedIndex_ = files_.empty() ? 0 : files_.size() - 1;
                  }
                }
              }
              currentScreen_ = Screen::Browse;
              needsRender_ = true;
              break;
            case Button::Back:
            case Button::Left:
              currentScreen_ = Screen::Browse;
              needsRender_ = true;
              break;
            default:
              break;
          }
        } else {
          // Normal browse mode
          switch (e.button) {
            case Button::Up:
              navigateUp(core);
              break;
            case Button::Down:
              navigateDown(core);
              break;
            case Button::Left:
              break;
            case Button::Right:
              promptDelete(core);
              break;
            case Button::Center:
              openSelected(core);
              break;
            case Button::Back:
              goBack(core);
              break;
            case Button::Power:
              break;
          }
        }
        break;

      default:
        break;
    }
  }

  // If a file was selected, transition to reader
  if (hasSelection_) {
    hasSelection_ = false;
    return StateTransition::to(StateId::Reader);
  }

  // Return to home if requested
  if (goHome_) {
    goHome_ = false;
    strcpy(currentDir_, "/");  // Reset for next entry
    return StateTransition::to(StateId::Home);
  }

  return StateTransition::stay(StateId::FileList);
}

void FileListState::render(Core& core) {
  if (!needsRender_) {
    return;
  }

  Theme& theme = THEME_MANAGER.mutableCurrent();

  if (currentScreen_ == Screen::ConfirmDelete) {
    ui::render(renderer_, theme, confirmView_);
    confirmView_.needsRender = false;
    needsRender_ = false;
    core.display.markDirty();
    return;
  }

  renderer_.clearScreen(theme.backgroundColor);

  // Title with page indicator
  char title[32];
  if (getTotalPages() > 1) {
    snprintf(title, sizeof(title), "Books (%d/%d)", getCurrentPage(), getTotalPages());
  } else {
    strcpy(title, "Books");
  }
  renderer_.drawCenteredText(theme.readerFontId, 10, title, theme.primaryTextBlack, BOLD);

  // Empty state
  if (files_.empty()) {
    renderer_.drawText(theme.uiFontId, 20, 60, "No books found", theme.primaryTextBlack);
    renderer_.displayBuffer();
    needsRender_ = false;
    core.display.markDirty();
    return;
  }

  // Draw current page of items
  constexpr int listStartY = 60;
  const int itemHeight = theme.itemHeight + theme.itemSpacing;
  const int pageItems = getPageItems();
  const int pageStart = getPageStartIndex();
  const int pageEnd = std::min(pageStart + pageItems, static_cast<int>(files_.size()));

  for (int i = pageStart; i < pageEnd; i++) {
    const int y = listStartY + (i - pageStart) * itemHeight;
    ui::fileEntry(renderer_, theme, y, files_[i].name.c_str(), files_[i].isDir,
                  static_cast<size_t>(i) == selectedIndex_);
  }

  // Button hints - "Home" if at root, "Back" if in subfolder
  const char* backLabel = isAtRoot() ? "Home" : "Back";
  ui::buttonBar(renderer_, theme, backLabel, "Open", "", "Delete");

  if (firstRender_) {
    renderer_.displayBuffer(EInkDisplay::HALF_REFRESH);
    firstRender_ = false;
  } else {
    renderer_.displayBuffer();
  }
  needsRender_ = false;
  core.display.markDirty();
}

void FileListState::navigateUp(Core& core) {
  if (files_.empty()) return;

  if (selectedIndex_ > 0) {
    selectedIndex_--;
  } else {
    selectedIndex_ = files_.size() - 1;  // Wrap to last item
  }
  needsRender_ = true;
}

void FileListState::navigateDown(Core& core) {
  if (files_.empty()) return;

  if (selectedIndex_ + 1 < files_.size()) {
    selectedIndex_++;
  } else {
    selectedIndex_ = 0;  // Wrap to first item
  }
  needsRender_ = true;
}

void FileListState::openSelected(Core& core) {
  if (files_.empty()) {
    return;
  }

  const FileEntry& entry = files_[selectedIndex_];

  // Build full path
  size_t dirLen = strlen(currentDir_);
  if (currentDir_[dirLen - 1] == '/') {
    snprintf(selectedPath_, sizeof(selectedPath_), "%s%s", currentDir_, entry.name.c_str());
  } else {
    snprintf(selectedPath_, sizeof(selectedPath_), "%s/%s", currentDir_, entry.name.c_str());
  }

  if (entry.isDir) {
    // Enter directory
    strncpy(currentDir_, selectedPath_, sizeof(currentDir_) - 1);
    currentDir_[sizeof(currentDir_) - 1] = '\0';
    selectedIndex_ = 0;
    loadFiles(core);
    needsRender_ = true;

    // Save directory for return after mode switch
    strncpy(core.settings.fileListDir, currentDir_, sizeof(core.settings.fileListDir) - 1);
    core.settings.fileListDir[sizeof(core.settings.fileListDir) - 1] = '\0';
    core.settings.fileListSelectedName[0] = '\0';
    core.settings.fileListSelectedIndex = 0;
  } else {
    // Save position for return
    strncpy(core.settings.fileListDir, currentDir_, sizeof(core.settings.fileListDir) - 1);
    core.settings.fileListDir[sizeof(core.settings.fileListDir) - 1] = '\0';
    strncpy(core.settings.fileListSelectedName, entry.name.c_str(), sizeof(core.settings.fileListSelectedName) - 1);
    core.settings.fileListSelectedName[sizeof(core.settings.fileListSelectedName) - 1] = '\0';
    core.settings.fileListSelectedIndex = selectedIndex_;

    // Select file - transition to Reader mode via restart
    Serial.printf("[FILES] Selected: %s\n", selectedPath_);
    showTransitionNotification("Opening book...");
    saveTransition(BootMode::READER, selectedPath_, ReturnTo::FILE_MANAGER);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    ESP.restart();
  }
}

void FileListState::goBack(Core& core) {
  // Navigate to parent directory or go home if at root
  if (strcmp(currentDir_, "/") == 0) {
    // At root - go back to Home
    goHome_ = true;
    return;
  }

  // Find last slash and truncate
  char* lastSlash = strrchr(currentDir_, '/');
  if (lastSlash && lastSlash != currentDir_) {
    *lastSlash = '\0';
  } else {
    strcpy(currentDir_, "/");
  }

  selectedIndex_ = 0;
  loadFiles(core);
  needsRender_ = true;
}

void FileListState::promptDelete(Core& core) {
  if (files_.empty()) return;

  const FileEntry& entry = files_[selectedIndex_];
  const char* typeStr = entry.isDir ? "folder" : "file";

  char line1[48];
  snprintf(line1, sizeof(line1), "Delete this %s?", typeStr);

  char line2[48];
  if (entry.name.length() > 40) {
    snprintf(line2, sizeof(line2), "%.37s...", entry.name.c_str());
  } else {
    strncpy(line2, entry.name.c_str(), sizeof(line2) - 1);
    line2[sizeof(line2) - 1] = '\0';
  }

  confirmView_.setup("Confirm Delete", line1, line2);
  currentScreen_ = Screen::ConfirmDelete;
  needsRender_ = true;
}

int FileListState::getPageItems() const {
  const Theme& theme = THEME_MANAGER.current();
  constexpr int listStartY = 60;
  constexpr int bottomMargin = 70;
  const int availableHeight = renderer_.getScreenHeight() - listStartY - bottomMargin;
  const int itemHeight = theme.itemHeight + theme.itemSpacing;
  return std::max(1, availableHeight / itemHeight);
}

int FileListState::getTotalPages() const {
  if (files_.empty()) return 1;
  const int pageItems = getPageItems();
  return (static_cast<int>(files_.size()) + pageItems - 1) / pageItems;
}

int FileListState::getCurrentPage() const {
  const int pageItems = getPageItems();
  return selectedIndex_ / pageItems + 1;
}

int FileListState::getPageStartIndex() const {
  const int pageItems = getPageItems();
  return (selectedIndex_ / pageItems) * pageItems;
}

}  // namespace papyrix
