#include "FileListState.h"

#include <Arduino.h>
#include <EInkDisplay.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <esp_system.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "../content/RecentBooksStore.h"
#include "../core/BootMode.h"
#include "../core/Core.h"
#include "../core/TrashPaths.h"
#include "../ui/Elements.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"

#define TAG "FILELIST"

namespace papyrix {

FileListState::FileListState(GfxRenderer& renderer)
    : renderer_(renderer),
      selectedIndex_(0),
      needsRender_(true),
      hasSelection_(false),
      goRecent_(false),
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
  LOG_INF(TAG, "Entering, dir: %s", currentDir_);

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
  goRecent_ = false;
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

void FileListState::exit(Core& core) { LOG_INF(TAG, "Exiting"); }

void FileListState::loadFiles(Core& core) {
  files_.clear();
  files_.reserve(512);  // Pre-allocate for large libraries

  FsFile dir;
  auto result = core.storage.openDir(currentDir_, dir);
  if (!result.ok()) {
    LOG_ERR(TAG, "Failed to open dir: %s", currentDir_);
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
    LOG_INF(TAG, "Warning: truncated to %zu entries", MAX_ENTRIES);
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

  LOG_INF(TAG, "Loaded %zu entries", files_.size());
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
  if (strcasecmp(ext, "html") == 0) return true;
  if (strcasecmp(ext, "htm") == 0) return true;
  return false;
}

bool FileListState::isTrashDirectory() const { return trash::isPath(currentDir_); }

bool FileListState::isTrashRootEntry() const {
  return isAtRoot() && selectedIndex_ < files_.size() && files_[selectedIndex_].isDir &&
         trash::isDirectoryName(files_[selectedIndex_].name.c_str());
}

bool FileListState::buildSelectedPath(char* path, size_t pathSize) const {
  if (files_.empty() || selectedIndex_ >= files_.size() || currentDir_[0] == '\0') return false;

  const size_t dirLen = strlen(currentDir_);
  const char* separator = currentDir_[dirLen - 1] == '/' ? "" : "/";
  const int written = snprintf(path, pathSize, "%s%s%s", currentDir_, separator, files_[selectedIndex_].name.c_str());
  return written >= 0 && static_cast<size_t>(written) < pathSize;
}

bool FileListState::findVacantPath(Core& core, const char* directory, const char* filename, char* path,
                                   size_t pathSize) const {
  return trash::findVacantPath(path, pathSize, directory, filename, [&](const char* candidate) {
    auto exists = core.storage.exists(candidate);
    if (!exists.ok()) return trash::PathProbe::Failed;
    return *exists ? trash::PathProbe::Occupied : trash::PathProbe::Vacant;
  });
}

void FileListState::setupFileConfirm(Screen screen, const char* title, const char* question) {
  char line[ui::ConfirmDialogView::MAX_LINE_LEN];
  const std::string& name = files_[selectedIndex_].name;
  size_t length = utf8SafeCopy(line, sizeof(line) - 4, name.c_str());
  if (length < name.size()) {
    line[length++] = '.';
    line[length++] = '.';
    line[length++] = '.';
  }
  line[length] = '\0';

  confirmView_.setup(title, question, line);
  currentScreen_ = screen;
  needsRender_ = true;
}

void FileListState::promptMoveToTrash() {
  setupFileConfirm(Screen::ConfirmMoveToTrash, tr(CONFIRM_TRASH), tr(MOVE_TO_TRASH_Q));
}

void FileListState::promptRestore() {
  setupFileConfirm(Screen::ConfirmRestore, tr(CONFIRM_RESTORE), tr(RESTORE_FILE_Q));
}

void FileListState::promptPermanentDelete() {
  setupFileConfirm(Screen::ConfirmPermanentDelete, tr(CONFIRM_DELETE), tr(DELETE_PERMANENTLY_Q));
}

void FileListState::promptDeleteEmptyDirectory() {
  setupFileConfirm(Screen::ConfirmDeleteEmptyDirectory, tr(CONFIRM_DELETE), tr(DELETE_FOLDER_Q));
}

void FileListState::executeConfirmedAction(Core& core) {
  const size_t sourcePathSize = isTrashDirectory() ? BufferSize::TrashPath : BufferSize::FilePath;
  if (!buildSelectedPath(selectedPath_, sourcePathSize)) {
    const char* failed = tr(DELETE_FAILED);
    if (currentScreen_ == Screen::ConfirmMoveToTrash) failed = tr(MOVE_TO_TRASH_FAILED);
    if (currentScreen_ == Screen::ConfirmRestore) failed = tr(RESTORE_FAILED);
    ui::centeredMessage(renderer_, THEME, THEME.uiFontId, failed);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    currentScreen_ = Screen::Browse;
    needsRender_ = true;
    return;
  }

  const FileEntry& entry = files_[selectedIndex_];
  const bool destructive =
      currentScreen_ == Screen::ConfirmMoveToTrash || currentScreen_ == Screen::ConfirmPermanentDelete;
  if (destructive && core.settings.lastBookPath[0] != '\0' && strcmp(selectedPath_, core.settings.lastBookPath) == 0) {
    ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(CANNOT_DELETE_ACTIVE));
    vTaskDelay(1500 / portTICK_PERIOD_MS);
  } else {
    bool success = false;
    const char* status = tr(DELETE_FAILED);

    if (currentScreen_ == Screen::ConfirmMoveToTrash) {
      bool trashReady = false;
      if (trash::buildTrashParent(currentDir_, sizeof(currentDir_), selectedPath_)) {
        const auto exists = core.storage.exists(currentDir_);
        trashReady = exists.ok() && (*exists || core.storage.mkdir(currentDir_).ok());
      }

      if (trashReady &&
          findVacantPath(core, currentDir_, entry.name.c_str(), actionDestination_, sizeof(actionDestination_))) {
        ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(MOVING_TO_TRASH));
        success = core.storage.rename(selectedPath_, actionDestination_).ok();
      }
      status = success ? tr(MOVED_TO_TRASH) : tr(MOVE_TO_TRASH_FAILED);
      if (success) {
        RecentBooksStore::instance().remove(selectedPath_);
      }
    } else if (currentScreen_ == Screen::ConfirmRestore) {
      const bool targetFound = trash::findRestorePath(
          actionDestination_, sizeof(actionDestination_), currentDir_, sizeof(currentDir_), selectedPath_,
          entry.name.c_str(),
          [&](const char* parent) {
            if (strcmp(parent, "/") == 0) return true;
            const auto exists = core.storage.exists(parent);
            if (!exists.ok()) return false;
            if (*exists) {
              const auto directory = core.storage.isDirectory(parent);
              return directory.ok() && *directory;
            }
            return core.storage.mkdir(parent).ok();
          },
          [&](const char* candidate) {
            const auto exists = core.storage.exists(candidate);
            if (!exists.ok()) return trash::PathProbe::Failed;
            return *exists ? trash::PathProbe::Occupied : trash::PathProbe::Vacant;
          });
      if (targetFound) {
        ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(RESTORING));
        success = core.storage.rename(selectedPath_, actionDestination_).ok();
      }
      status = success ? tr(RESTORED) : tr(RESTORE_FAILED);
    } else if (currentScreen_ == Screen::ConfirmPermanentDelete) {
      ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(DELETING));
      success = core.storage.remove(selectedPath_).ok();
      status = success ? tr(DELETED) : tr(DELETE_FAILED);
    } else if (currentScreen_ == Screen::ConfirmDeleteEmptyDirectory) {
      ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(DELETING));
      success = core.storage.rmdirEmpty(selectedPath_).ok();
      status = success ? tr(DELETED) : tr(DELETE_FAILED);
    }

    ui::centeredMessage(renderer_, THEME, THEME.uiFontId, status);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  if (!trash::buildSourceParent(currentDir_, sizeof(currentDir_), selectedPath_)) {
    strcpy(currentDir_, "/");
  }
  loadFiles(core);
  if (selectedIndex_ >= files_.size()) {
    selectedIndex_ = files_.empty() ? 0 : files_.size() - 1;
  }
  currentScreen_ = Screen::Browse;
  needsRender_ = true;
}

StateTransition FileListState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
    switch (e.type) {
      case EventType::ButtonRepeat:
        if (currentScreen_ == Screen::Browse) {
          if (e.button == Button::Up)
            navigateUp(core);
          else if (e.button == Button::Down)
            navigateDown(core);
        }
        break;

      case EventType::ButtonPress:
        if (currentScreen_ != Screen::Browse) {
          switch (e.button) {
            case Button::Up:
            case Button::Down:
            case Button::Left:
            case Button::Right:
              confirmView_.toggleSelection();
              needsRender_ = true;
              break;
            case Button::Center:
              if (confirmView_.isYesSelected()) {
                executeConfirmedAction(core);
              } else {
                currentScreen_ = Screen::Browse;
                needsRender_ = true;
              }
              break;
            case Button::Back:
              currentScreen_ = Screen::Browse;
              needsRender_ = true;
              break;
            default:
              break;
          }
        } else {
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
              if (files_.empty()) break;
              if (isTrashRootEntry()) {
                ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(CANNOT_DELETE_TRASH));
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                needsRender_ = true;
              } else {
                switch (trash::deleteAction(files_[selectedIndex_].isDir, isTrashDirectory())) {
                  case trash::DeleteAction::MoveToTrash:
                    promptMoveToTrash();
                    break;
                  case trash::DeleteAction::PermanentlyDelete:
                    promptPermanentDelete();
                    break;
                  case trash::DeleteAction::DeleteEmptyDirectory:
                    promptDeleteEmptyDirectory();
                    break;
                }
              }
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

  if (hasSelection_) {
    hasSelection_ = false;
    return StateTransition::to(StateId::Reader);
  }

  if (goRecent_) {
    goRecent_ = false;
    strcpy(currentDir_, "/");
    return StateTransition::to(core.settings.showRecents ? StateId::Recent : StateId::Home);
  }

  return StateTransition::stay(StateId::FileList);
}

void FileListState::render(Core& core) {
  if (!needsRender_) {
    return;
  }

  Theme& theme = THEME_MANAGER.mutableCurrent();

  if (currentScreen_ != Screen::Browse) {
    ui::render(renderer_, theme, confirmView_);
    confirmView_.needsRender = false;
    needsRender_ = false;
    core.display.markDirty();
    return;
  }

  renderer_.clearScreen(theme.backgroundColor);

  // Title with page indicator
  char title[48];
  if (getTotalPages() > 1) {
    snprintf(title, sizeof(title), "%s (%d/%d)", tr(FILES), getCurrentPage(), getTotalPages());
  } else {
    strncpy(title, tr(FILES), sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
  }
  renderer_.drawCenteredText(theme.readerFontId, 10, title, theme.primaryTextBlack, BOLD);

  // Empty state
  if (files_.empty()) {
    renderer_.drawText(theme.uiFontId, 20, 60, tr(NO_BOOKS_FOUND), theme.primaryTextBlack);
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

  const char* backLabel = isAtRoot() ? (core.settings.showRecents ? tr(BOOKS) : tr(HOME)) : tr(BACK);
  const bool restoreSelected = isTrashDirectory() && !files_[selectedIndex_].isDir;
  ui::buttonBar(renderer_, theme, backLabel, restoreSelected ? tr(RESTORE) : tr(OPEN), "", tr(DELETE_BTN));

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

  const size_t pathSize = isTrashDirectory() ? BufferSize::TrashPath : BufferSize::FilePath;
  if (!buildSelectedPath(selectedPath_, pathSize)) {
    return;
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
    if (isTrashDirectory()) {
      promptRestore();
      return;
    }

    // Save position for return
    strncpy(core.settings.fileListDir, currentDir_, sizeof(core.settings.fileListDir) - 1);
    core.settings.fileListDir[sizeof(core.settings.fileListDir) - 1] = '\0';
    strncpy(core.settings.fileListSelectedName, entry.name.c_str(), sizeof(core.settings.fileListSelectedName) - 1);
    core.settings.fileListSelectedName[sizeof(core.settings.fileListSelectedName) - 1] = '\0';
    core.settings.fileListSelectedIndex = selectedIndex_;

    // Select file - transition to Reader mode via restart
    LOG_INF(TAG, "Selected: %s", selectedPath_);
    showTransitionNotification(tr(OPENING_BOOK));
    saveTransition(BootMode::READER, selectedPath_, ReturnTo::FILE_MANAGER);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    ESP.restart();
  }
}

void FileListState::goBack(Core& core) {
  // Navigate to parent directory or return to Recent if at root
  if (strcmp(currentDir_, "/") == 0) {
    // At root - go back to Recent
    goRecent_ = true;
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
