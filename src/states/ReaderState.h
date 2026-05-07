#pragma once

#include <BackgroundTask.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "../content/ReaderNavigation.h"
#include "../core/Types.h"
#include "../rendering/XtcPageRenderer.h"
#include "State.h"

class ContentParser;
class GfxRenderer;
class PageCache;
class Page;
struct RenderConfig;

namespace papyrix {

// Forward declarations
class Core;
struct Event;

// ReaderState - unified reader for all content types
// Uses ContentHandle to abstract Epub/Xtc/Txt/Markdown differences
// Uses PageCache for all formats with partial caching support
// Delegates to: XtcPageRenderer (binary rendering), ProgressManager (persistence),
//               ReaderNavigation (page traversal)
class ReaderState : public State {
 public:
  explicit ReaderState(GfxRenderer& renderer);
  ~ReaderState() override;

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::Reader; }

  // Set content path before entering state
  void setContentPath(const char* path);

  // Reading position
  uint32_t currentPage() const { return currentPage_; }
  void setCurrentPage(uint32_t page) { currentPage_ = page; }

 private:
  GfxRenderer& renderer_;
  XtcPageRenderer xtcRenderer_;
  char contentPath_[256];
  uint32_t currentPage_;
  bool needsRender_;
  bool contentLoaded_;
  bool loadFailed_ = false;  // Track if content loading failed (for error state transition)

  // Reading position (maps to ReaderNavigation::Position)
  int currentSpineIndex_;
  int currentSectionPage_;

  // Last successfully rendered position (for accurate progress saving)
  int lastRenderedSpineIndex_ = 0;
  int lastRenderedSectionPage_ = 0;

  // Whether book has a valid cover image
  bool hasCover_ = false;

  // First text content spine index (from EPUB guide, 0 if not specified)
  int textStartIndex_ = 0;

  // Unified page cache for all content types
  // Ownership model: main thread owns pageCache_/parser_ when !cacheTask_.isRunning()
  //                  background task owns pageCache_/parser_ when cacheTask_.isRunning()
  // Navigation ALWAYS stops task first, then accesses cache/parser
  std::unique_ptr<PageCache> pageCache_;

  // Persistent parser for incremental (hot) extends — kept alive between extend calls
  // so the parser can resume from where it left off instead of re-parsing from byte 0
  std::unique_ptr<ContentParser> parser_;
  int parserSpineIndex_ = -1;
  uint8_t pagesUntilFullRefresh_;

  // Background caching (uses BackgroundTask for proper lifecycle management)
  BackgroundTask cacheTask_;
  Core* coreForCacheTask_ = nullptr;
  bool thumbnailDone_ = false;
  void startBackgroundCaching(Core& core);
  void stopBackgroundCaching();

  // Navigation helpers (delegates to ReaderNavigation)
  void navigateNext(Core& core);
  void navigatePrev(Core& core);
  void navigateNextChapter(Core& core);
  void navigatePrevChapter(Core& core);
  void applyNavResult(const ReaderNavigation::NavResult& result, Core& core);

  // Track whether a chapter jump already fired during a button hold
  bool holdNavigated_ = false;

  // Track power press start when short power action is mapped to page turn.
  // This lets us execute page turn only on short release and avoid accidental
  // turns when the same press is held to enter sleep.
  uint32_t powerPressStartedMs_ = 0;

  // Rendering
  void renderCurrentPage(Core& core);
  void renderCachedPage(Core& core);
  void renderXtcPage(Core& core);
  bool renderCoverPage(Core& core);

  // Helpers
  void renderPageContents(Core& core, Page& page, int marginTop, int marginRight, int marginBottom, int marginLeft);

  // Global page metrics — whole-book page counting for EPUB/FB2
  struct GlobalPageMetrics {
    int currentPage = 1;
    int totalPages = 0;
    bool totalIsExact = true;
  };
  struct SectionPageMetric {
    uint16_t pages = 0;
    bool exact = false;
    uint32_t byteSize = 0;
  };
  void invalidateGlobalPageMetrics();
  void initializeGlobalPageMetrics(Core& core);
  void updateGlobalPageMetrics(Core& core);
  void recalibrateGlobalPageEstimates();
  void recomputeGlobalPageMetricTotal();
  GlobalPageMetrics resolveGlobalPageMetrics(Core& core);

  std::vector<SectionPageMetric> globalSectionPageMetrics_;
  uint32_t globalSectionPageMetricTotal_ = 0;
  bool globalSectionPageMetricsInitialized_ = false;

  // Cache management
  bool ensurePageCached(Core& core, uint16_t pageNum);
  void loadCacheFromDisk(Core& core);
  void createOrExtendCache(Core& core);

  void createOrExtendCacheImpl(ContentParser& parser, const std::string& cachePath, const RenderConfig& config);
  void backgroundCacheImpl(ContentParser& parser, const std::string& cachePath, const RenderConfig& config);
  void saveAnchorMap(const ContentParser& parser, const std::string& cachePath);
  int loadAnchorPage(const std::string& cachePath, const std::string& anchor);

  // Display helpers
  void displayWithRefresh(Core& core);

  // Viewport calculation
  struct Viewport {
    int marginTop;
    int marginRight;
    int marginBottom;
    int marginLeft;
    int width;
    int height;
  };
  Viewport getReaderViewport() const;

  // Get first content spine index (skips cover document when appropriate)
  static int calcFirstContentSpine(bool hasCover, int textStartIndex, size_t spineCount);

  // Source state (where reader was opened from)
  StateId sourceState_ = StateId::Home;

  // Boot mode transition - exit to UI via restart
  void exitToUI(Core& core);

  void toggleReaderOrientation(Core& core);

  // Center button click handling:
  // - single click toggles orientation after a short delay
  // - double click opens Settings
  bool centerClickPending_ = false;
  uint32_t centerClickStartedMs_ = 0;
  static constexpr uint32_t kCenterDoubleClickMs_ = 450;
};

}  // namespace papyrix
