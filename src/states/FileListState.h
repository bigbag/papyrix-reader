#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../ui/views/SettingsViews.h"
#include "State.h"

class GfxRenderer;

namespace papyrix {

// FileListState - browse and select files
// Uses dynamic vector for unlimited file support with pagination
class FileListState : public State {
  enum class Screen : uint8_t {
    Browse,
    ConfirmDelete,
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
  char currentDir_[512];
  char selectedPath_[512];

  // File entries - dynamic vector for unlimited files
  struct FileEntry {
    std::string name;
    bool isDir;
  };
  std::vector<FileEntry> files_;

  size_t selectedIndex_;
  bool needsRender_;
  bool hasSelection_;
  bool goHome_;       // Return to Home state
  bool firstRender_;  // Use HALF_REFRESH on first render to clear ghosting
  Screen currentScreen_;
  ui::ConfirmDialogView confirmView_;

  void loadFiles(Core& core);
  void promptDelete(Core& core);
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
