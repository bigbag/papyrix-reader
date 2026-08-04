#include "FileListState.h"

#include <Arduino.h>
#include <EInkDisplay.h>
#include <FileIndex.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <algorithm>
#include <cstring>
#include <new>

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

  const size_t count = entryCount();
  if (preservePosition && count > 0) {
    selectedIndex_ = std::min<size_t>(core.settings.fileListSelectedIndex, count - 1);

    FileEntryView selected{};
    if (!entryAt(selectedIndex_, selected) || strcasecmp(selected.name, core.settings.fileListSelectedName) != 0) {
      const size_t restoredIndex = findEntryByName(core.settings.fileListSelectedName);
      if (restoredIndex < count) selectedIndex_ = restoredIndex;
    }
  } else {
    selectedIndex_ = 0;
  }
}

void FileListState::exit(Core& core) {
  LOG_INF(TAG, "Exiting");
  fileIndex_.reset();
  std::vector<FileEntry>().swap(files_);
}

void FileListState::loadFiles(Core& core) {
  fileIndex_.reset();
  files_.clear();

  const size_t reserveBytes = IN_MEMORY_ENTRY_LIMIT * sizeof(FileEntry);
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const bool canBufferEntries = files_.capacity() >= IN_MEMORY_ENTRY_LIMIT || reserveBytes <= largestBlock * 80 / 100;

  if (canBufferEntries && files_.capacity() < IN_MEMORY_ENTRY_LIMIT) files_.reserve(IN_MEMORY_ENTRY_LIMIT);

  bool overflow = false;
  const bool scanned = canBufferEntries && scanFiles(core, IN_MEMORY_ENTRY_LIMIT, overflow);
  if (scanned && !overflow) {
    std::sort(files_.begin(), files_.end(), [](const FileEntry& a, const FileEntry& b) {
      if (a.isDir != b.isDir) return a.isDir;
      return FsHelpers::naturalCompare(a.name.c_str(), b.name.c_str()) < 0;
    });
    LOG_INF(TAG, "Loaded %zu entries", files_.size());
    return;
  }

  std::vector<FileEntry>().swap(files_);
  FileIndex* index = new (std::nothrow) FileIndex();
  if (index) {
    fileIndex_.reset(index);
    if (fileIndex_->open(currentDir_, acceptEntry)) {
      LOG_INF(TAG, "Loaded %zu indexed entries", fileIndex_->size());
      return;
    }
    fileIndex_.reset();
  }

  const size_t fallbackLargestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const bool canBufferFallback =
      files_.capacity() >= IN_MEMORY_ENTRY_LIMIT || reserveBytes <= fallbackLargestBlock * 80 / 100;
  if (!canBufferFallback) {
    LOG_ERR(TAG, "Insufficient heap to list %s", currentDir_);
    return;
  }
  if (files_.capacity() < IN_MEMORY_ENTRY_LIMIT) files_.reserve(IN_MEMORY_ENTRY_LIMIT);

  bool ignoredOverflow = false;
  if (scanFiles(core, IN_MEMORY_ENTRY_LIMIT, ignoredOverflow)) {
    std::sort(files_.begin(), files_.end(), [](const FileEntry& a, const FileEntry& b) {
      if (a.isDir != b.isDir) return a.isDir;
      return FsHelpers::naturalCompare(a.name.c_str(), b.name.c_str()) < 0;
    });
  }
  LOG_ERR(TAG, "Index failed for %s; showing first %zu entries", currentDir_, files_.size());
}

bool FileListState::scanFiles(Core& core, size_t limit, bool& overflow) {
  overflow = false;
  FsFile directory;
  const auto result = core.storage.openDir(currentDir_, directory);
  if (!result.ok()) {
    LOG_ERR(TAG, "Failed to open dir: %s", currentDir_);
    return false;
  }

  char name[FileIndex::MAX_NAME + 1];
  while (true) {
    FsFile entry = directory.openNextFile();
    if (!entry) break;

    entry.getName(name, sizeof(name));
    const bool isDirectory = entry.isDirectory();
    entry.close();
    if (!acceptEntry(name, isDirectory)) continue;

    if (files_.size() >= limit) {
      overflow = true;
      break;
    }
    files_.push_back({name, isDirectory});
  }
  directory.close();
  return true;
}

size_t FileListState::entryCount() const { return fileIndex_ ? fileIndex_->size() : files_.size(); }

bool FileListState::entryAt(size_t index, FileEntryView& out) {
  if (!fileIndex_) {
    if (index >= files_.size()) return false;
    out = {files_[index].name.c_str(), files_[index].isDir};
    return true;
  }

  if (!fileIndex_->entryAt(index, indexedEntry_)) return false;
  out = {indexedEntry_.name, indexedEntry_.isDir};
  return true;
}

size_t FileListState::findEntryByName(const char* name) {
  if (!name) return entryCount();
  if (fileIndex_) {
    const size_t row = fileIndex_->findRowByName(name);
    return row == SIZE_MAX ? entryCount() : row;
  }

  for (size_t i = 0; i < files_.size(); i++) {
    if (strcasecmp(files_[i].name.c_str(), name) == 0) return i;
  }
  return files_.size();
}

bool FileListState::acceptEntry(const char* name, bool isDir) {
  return name && !isHidden(name) && (isDir || isSupportedFile(name));
}

bool FileListState::isHidden(const char* name) {
  if (name[0] == '.') return true;
  if (FsHelpers::isHiddenFsItem(name)) return true;
  if (strncmp(name, "FOUND.", 6) == 0) return true;
  return false;
}

bool FileListState::isSupportedFile(const char* name) {
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

bool FileListState::isTrashRootEntry() {
  FileEntryView entry{};
  return isAtRoot() && entryAt(selectedIndex_, entry) && entry.isDir && trash::isDirectoryName(entry.name);
}

bool FileListState::buildSelectedPath(char* path, size_t pathSize) {
  FileEntryView entry{};
  if (!entryAt(selectedIndex_, entry) || currentDir_[0] == '\0') return false;

  const size_t dirLen = strlen(currentDir_);
  const char* separator = currentDir_[dirLen - 1] == '/' ? "" : "/";
  const int written = snprintf(path, pathSize, "%s%s%s", currentDir_, separator, entry.name);
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
  FileEntryView entry{};
  if (!entryAt(selectedIndex_, entry)) return;

  char line[ui::ConfirmDialogView::MAX_LINE_LEN];
  const size_t nameLength = strlen(entry.name);
  size_t length = utf8SafeCopy(line, sizeof(line) - 4, entry.name);
  if (length < nameLength) {
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

  FileEntryView entry{};
  if (!entryAt(selectedIndex_, entry)) {
    currentScreen_ = Screen::Browse;
    needsRender_ = true;
    return;
  }

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

      if (trashReady && findVacantPath(core, currentDir_, entry.name, actionDestination_, sizeof(actionDestination_))) {
        ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(MOVING_TO_TRASH));
        success = core.storage.rename(selectedPath_, actionDestination_).ok();
      }
      status = success ? tr(MOVED_TO_TRASH) : tr(MOVE_TO_TRASH_FAILED);
      if (success) {
        RecentBooksStore::instance().remove(selectedPath_);
      }
    } else if (currentScreen_ == Screen::ConfirmRestore) {
      const bool targetFound = trash::findRestorePath(
          actionDestination_, sizeof(actionDestination_), currentDir_, sizeof(currentDir_), selectedPath_, entry.name,
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
  const size_t count = entryCount();
  if (selectedIndex_ >= count) selectedIndex_ = count == 0 ? 0 : count - 1;
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
            case Button::Right: {
              FileEntryView entry{};
              if (!entryAt(selectedIndex_, entry)) break;
              if (isTrashRootEntry()) {
                ui::centeredMessage(renderer_, THEME, THEME.uiFontId, tr(CANNOT_DELETE_TRASH));
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                needsRender_ = true;
              } else {
                switch (trash::deleteAction(entry.isDir, isTrashDirectory())) {
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
            }
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

  const size_t count = entryCount();

  // Empty state
  if (count == 0) {
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
  const int pageEnd = std::min(pageStart + pageItems, static_cast<int>(count));

  for (int i = pageStart; i < pageEnd; i++) {
    FileEntryView entry{};
    if (!entryAt(static_cast<size_t>(i), entry)) continue;
    const int y = listStartY + (i - pageStart) * itemHeight;
    ui::fileEntry(renderer_, theme, y, entry.name, entry.isDir, static_cast<size_t>(i) == selectedIndex_);
  }

  FileEntryView selected{};
  const bool hasSelected = entryAt(selectedIndex_, selected);
  const char* backLabel = isAtRoot() ? (core.settings.showRecents ? tr(BOOKS) : tr(HOME)) : tr(BACK);
  const bool restoreSelected = hasSelected && isTrashDirectory() && !selected.isDir;
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
  const size_t count = entryCount();
  if (count == 0) return;

  if (selectedIndex_ > 0) {
    selectedIndex_--;
  } else {
    selectedIndex_ = count - 1;  // Wrap to last item
  }
  needsRender_ = true;
}

void FileListState::navigateDown(Core& core) {
  const size_t count = entryCount();
  if (count == 0) return;

  if (selectedIndex_ + 1 < count) {
    selectedIndex_++;
  } else {
    selectedIndex_ = 0;  // Wrap to first item
  }
  needsRender_ = true;
}

void FileListState::openSelected(Core& core) {
  FileEntryView entry{};
  if (!entryAt(selectedIndex_, entry)) return;

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
    strncpy(core.settings.fileListSelectedName, entry.name, sizeof(core.settings.fileListSelectedName) - 1);
    core.settings.fileListSelectedName[sizeof(core.settings.fileListSelectedName) - 1] = '\0';
    core.settings.fileListSelectedIndex = static_cast<uint16_t>(std::min<size_t>(selectedIndex_, UINT16_MAX));

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
  const size_t count = entryCount();
  if (count == 0) return 1;
  const int pageItems = getPageItems();
  return (static_cast<int>(count) + pageItems - 1) / pageItems;
}

int FileListState::getCurrentPage() const {
  const size_t pageItems = static_cast<size_t>(getPageItems());
  return static_cast<int>(selectedIndex_ / pageItems) + 1;
}

int FileListState::getPageStartIndex() const {
  const size_t pageItems = static_cast<size_t>(getPageItems());
  return static_cast<int>((selectedIndex_ / pageItems) * pageItems);
}

}  // namespace papyrix
