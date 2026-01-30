#pragma once

#include <cstdint>
#include <cstring>

#include "../ui/views/SettingsViews.h"
#include "State.h"

class GfxRenderer;

namespace papyrix {

// FileListState - browse and select files
// Uses fixed-size arrays to avoid heap allocation
class FileListState : public State {
  enum class Screen : uint8_t {
    Browse,
    ConfirmDelete,
  };

 public:
  static constexpr uint16_t MAX_FILES = 64;
  static constexpr uint16_t MAX_NAME_LEN = 128;  // UTF-8 aware: ~40 Cyrillic or 128 ASCII chars

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
  char currentDir_[256];
  char selectedPath_[256];

  // File entries - fixed size array
  struct FileEntry {
    char name[MAX_NAME_LEN];
    bool isDir;
  };
  FileEntry files_[MAX_FILES];
  uint16_t fileCount_;

  uint16_t selectedIndex_;
  uint16_t scrollOffset_;
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
  void ensureVisible(int visibleCount);
  int getVisibleCount() const;
  bool isHidden(const char* name) const;
  bool isSupportedFile(const char* name) const;
  bool isAtRoot() const { return strcmp(currentDir_, "/") == 0; }
};

}  // namespace papyrix
