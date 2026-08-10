#pragma once

#include <FileIndex.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../core/Types.h"
#include "../ui/views/SettingsViews.h"
#include "State.h"

class GfxRenderer;

namespace papyrix {

// FileListState - browse and select files
// Uses dynamic vector for unlimited file support with pagination
class FileListState : public State {
  enum class Screen : uint8_t {
    Browse,
    ConfirmMoveToTrash,
    ConfirmRestore,
    ConfirmPermanentDelete,
    ConfirmDeleteDirectory,
  };

 public:
  explicit FileListState(GfxRenderer& renderer);
  ~FileListState() override;

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::FileList; }

  // Get selected file path after state exits
  const char* selectedPath() const { return selectedPath_; }

  // Set initial directory before entering
  void setDirectory(const char* dir);

 private:
  GfxRenderer& renderer_;
  char currentDir_[BufferSize::TrashPath];
  char selectedPath_[BufferSize::TrashPath];
  char actionDestination_[BufferSize::TrashPath] = {};

  struct FileEntry {
    std::string name;
    bool isDir;
  };
  struct FileEntryView {
    const char* name;
    bool isDir;
  };

  static constexpr size_t IN_MEMORY_ENTRY_LIMIT = 128;
  std::vector<FileEntry> files_;
  std::unique_ptr<FileIndex> fileIndex_;
  FileIndex::Entry indexedEntry_{};

  size_t selectedIndex_;
  bool needsRender_;
  bool hasSelection_;
  bool goRecent_;     // Return to Recent state (parent of Files)
  bool firstRender_;  // Use HALF_REFRESH on first render to clear ghosting
  Screen currentScreen_;
  ui::ConfirmDialogView confirmView_;

  void loadFiles(Core& core);
  bool scanFiles(Core& core, size_t limit, bool& overflow);
  size_t entryCount() const;
  bool entryAt(size_t index, FileEntryView& out);
  size_t findEntryByName(const char* name);
  static bool acceptEntry(const char* name, bool isDir);
  bool isTrashDirectory() const;
  bool isTrashRootEntry();
  bool buildSelectedPath(char* path, size_t pathSize);
  bool findVacantPath(Core& core, const char* directory, const char* filename, char* path, size_t pathSize) const;
  void setupFileConfirm(Screen screen, const char* title, const char* question);
  void promptMoveToTrash();
  void promptRestore();
  void promptPermanentDelete();
  void promptDeleteDirectory();
  void executeConfirmedAction(Core& core);
  void navigateUp(Core& core);
  void navigateDown(Core& core);
  void openSelected(Core& core);
  void goBack(Core& core);

  // Pagination helpers
  int getPageItems() const;
  int getTotalPages() const;
  int getCurrentPage() const;
  int getPageStartIndex() const;

  static bool isHidden(const char* name);
  static bool isSupportedFile(const char* name);
  bool isAtRoot() const { return strcmp(currentDir_, "/") == 0; }
};

}  // namespace papyrix
