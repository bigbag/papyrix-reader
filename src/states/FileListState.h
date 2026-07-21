#pragma once

#include <cstdint>
#include <cstring>
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
    ConfirmDeleteEmptyDirectory,
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

  // File entries - dynamic vector for unlimited files
  struct FileEntry {
    std::string name;
    bool isDir;
  };
  std::vector<FileEntry> files_;

  size_t selectedIndex_;
  bool needsRender_;
  bool hasSelection_;
  bool goRecent_;     // Return to Recent state (parent of Files)
  bool firstRender_;  // Use HALF_REFRESH on first render to clear ghosting
  Screen currentScreen_;
  ui::ConfirmDialogView confirmView_;

  void loadFiles(Core& core);
  bool isTrashDirectory() const;
  bool isTrashRootEntry() const;
  bool buildSelectedPath(char* path, size_t pathSize) const;
  bool findVacantPath(Core& core, const char* directory, const char* filename, char* path, size_t pathSize) const;
  void setupFileConfirm(Screen screen, const char* title, const char* question);
  void promptMoveToTrash();
  void promptRestore();
  void promptPermanentDelete();
  void promptDeleteEmptyDirectory();
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

  bool isHidden(const char* name) const;
  bool isSupportedFile(const char* name) const;
  bool isAtRoot() const { return strcmp(currentDir_, "/") == 0; }
};

}  // namespace papyrix
