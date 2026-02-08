// ImagePageLayout unit tests
// Tests the addImageToPage layout logic: tall image detection, dedicated pages,
// vertical centering, and page flushing behavior.
//
// Reimplements the layout algorithm from ChapterHtmlSlimParser::addImageToPage
// in a test-friendly way without needing the full rendering infrastructure.

#include "test_utils.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ImageInfo {
  uint16_t width;
  uint16_t height;
};

struct PageElement {
  int xPos;
  int yPos;
  uint16_t imageWidth;
  uint16_t imageHeight;
};

struct Page {
  std::vector<PageElement> elements;
};

// Reimplements the addImageToPage layout algorithm for testing
class ImageLayoutEngine {
 public:
  int viewportWidth;
  int viewportHeight;
  int lineHeight;

  std::unique_ptr<Page> currentPage;
  int currentPageNextY = 0;
  std::vector<std::unique_ptr<Page>> completedPages;
  bool stopRequested = false;

  ImageLayoutEngine(int vw, int vh, int lh) : viewportWidth(vw), viewportHeight(vh), lineHeight(lh) {}

  void addImage(const ImageInfo& image) {
    if (stopRequested) return;

    const int imageHeight = image.height;
    const bool isTallImage = imageHeight > viewportHeight / 2;

    if (!currentPage) {
      currentPage = std::make_unique<Page>();
      currentPageNextY = 0;
    }

    // Tall images get a dedicated page: flush current page if it has content
    if (isTallImage && currentPageNextY > 0) {
      completedPages.push_back(std::move(currentPage));
      currentPage = std::make_unique<Page>();
      currentPageNextY = 0;
    }

    // Check if image fits on current page
    if (currentPageNextY + imageHeight > viewportHeight) {
      completedPages.push_back(std::move(currentPage));
      currentPage = std::make_unique<Page>();
      currentPageNextY = 0;
    }

    // Center image horizontally
    int xPos = (viewportWidth - static_cast<int>(image.width)) / 2;
    if (xPos < 0) xPos = 0;

    // Center tall images vertically on their dedicated page
    int yPos = currentPageNextY;
    if (isTallImage && currentPageNextY == 0 && imageHeight < viewportHeight) {
      yPos = (viewportHeight - imageHeight) / 2;
    }

    currentPage->elements.push_back({xPos, yPos, image.width, image.height});
    currentPageNextY = yPos + imageHeight + lineHeight;

    // Complete the page after a tall image
    if (isTallImage) {
      completedPages.push_back(std::move(currentPage));
      currentPage = std::make_unique<Page>();
      currentPageNextY = 0;
    }
  }

  // Simulate adding text content that occupies vertical space
  void addTextBlock(int height) {
    if (!currentPage) {
      currentPage = std::make_unique<Page>();
      currentPageNextY = 0;
    }
    currentPageNextY += height;
  }

  int totalPages() const {
    int count = static_cast<int>(completedPages.size());
    if (currentPage && (!currentPage->elements.empty() || currentPageNextY > 0)) {
      count++;
    }
    return count;
  }
};

int main() {
  TestUtils::TestRunner runner("ImagePageLayout");

  const int VP_WIDTH = 480;
  const int VP_HEIGHT = 800;
  const int LINE_HEIGHT = 20;

  // Test 1: Small image placed on current page without page break
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo small = {200, 300};  // 300 <= 800/2 = 400, not tall
    engine.addImage(small);

    runner.expectEq(0, (int)engine.completedPages.size(), "small_image: no completed pages");
    runner.expectTrue(engine.currentPage != nullptr, "small_image: current page exists");
    runner.expectEq(1, (int)engine.currentPage->elements.size(), "small_image: one element on page");
    runner.expectEq(300 + LINE_HEIGHT, engine.currentPageNextY, "small_image: nextY advanced by image+lineHeight");
  }

  // Test 2: Small image is horizontally centered
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo small = {200, 300};
    engine.addImage(small);

    int expectedX = (VP_WIDTH - 200) / 2;
    runner.expectEq(expectedX, engine.currentPage->elements[0].xPos, "small_image_centering: horizontally centered");
    runner.expectEq(0, engine.currentPage->elements[0].yPos, "small_image_centering: starts at top");
  }

  // Test 3: Tall image gets a dedicated page
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo tall = {400, 500};  // 500 > 800/2 = 400, is tall
    engine.addImage(tall);

    runner.expectEq(1, (int)engine.completedPages.size(), "tall_image_dedicated: page completed after tall image");
    runner.expectEq(1, (int)engine.completedPages[0]->elements.size(), "tall_image_dedicated: image on completed page");
    runner.expectEq(0, engine.currentPageNextY, "tall_image_dedicated: nextY reset for new page");
  }

  // Test 4: Tall image is vertically centered on its dedicated page
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo tall = {400, 500};  // < viewportHeight, should be centered
    engine.addImage(tall);

    int expectedY = (VP_HEIGHT - 500) / 2;
    runner.expectEq(expectedY, engine.completedPages[0]->elements[0].yPos,
                    "tall_image_v_center: vertically centered");
  }

  // Test 5: Full-height tall image is NOT vertically centered (no room)
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo fullHeight = {400, VP_HEIGHT};  // exactly viewport height
    engine.addImage(fullHeight);

    runner.expectEq(0, engine.completedPages[0]->elements[0].yPos,
                    "full_height_image: not centered (imageHeight == viewportHeight)");
  }

  // Test 6: Tall image flushes current page if it has content
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    engine.addTextBlock(100);  // simulate some text on page
    ImageInfo tall = {400, 500};
    engine.addImage(tall);

    // Should have: page with text (flushed), page with tall image (completed)
    runner.expectEq(2, (int)engine.completedPages.size(), "tall_flush: two completed pages");
    runner.expectEq(0, (int)engine.completedPages[0]->elements.size(),
                    "tall_flush: first page has no image elements (text only)");
    runner.expectEq(1, (int)engine.completedPages[1]->elements.size(),
                    "tall_flush: second page has the tall image");
  }

  // Test 7: Tall image on empty page does NOT flush
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo tall = {400, 500};
    engine.addImage(tall);

    // Only 1 completed page (the tall image page itself)
    runner.expectEq(1, (int)engine.completedPages.size(), "tall_no_flush_empty: one completed page");
  }

  // Test 8: Small image that doesn't fit triggers page break
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    engine.addTextBlock(700);  // 700 of 800 used
    ImageInfo small = {200, 200};  // 700+200 > 800
    engine.addImage(small);

    runner.expectEq(1, (int)engine.completedPages.size(), "small_overflow: page break on overflow");
    runner.expectEq(0, (int)engine.completedPages[0]->elements.size(),
                    "small_overflow: first page has no image elements");
    runner.expectEq(1, (int)engine.currentPage->elements.size(), "small_overflow: image on new page");
    runner.expectEq(0, engine.currentPage->elements[0].yPos, "small_overflow: image at top of new page");
  }

  // Test 9: Multiple small images stack on same page
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo small1 = {200, 100};
    ImageInfo small2 = {200, 100};
    engine.addImage(small1);
    engine.addImage(small2);

    runner.expectEq(0, (int)engine.completedPages.size(), "stacked_small: no page break");
    runner.expectEq(2, (int)engine.currentPage->elements.size(), "stacked_small: two images on page");
    runner.expectEq(0, engine.currentPage->elements[0].yPos, "stacked_small: first at top");
    runner.expectEq(100 + LINE_HEIGHT, engine.currentPage->elements[1].yPos, "stacked_small: second below first");
  }

  // Test 10: Two tall images create separate pages
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo tall1 = {400, 500};
    ImageInfo tall2 = {400, 600};
    engine.addImage(tall1);
    engine.addImage(tall2);

    runner.expectEq(2, (int)engine.completedPages.size(), "two_tall: two completed pages");
    runner.expectEq(1, (int)engine.completedPages[0]->elements.size(), "two_tall: first page has image");
    runner.expectEq(1, (int)engine.completedPages[1]->elements.size(), "two_tall: second page has image");
  }

  // Test 11: Text after tall image goes on new page
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo tall = {400, 500};
    engine.addImage(tall);
    engine.addTextBlock(50);

    runner.expectEq(1, (int)engine.completedPages.size(), "text_after_tall: tall image page completed");
    runner.expectEq(50, engine.currentPageNextY, "text_after_tall: text on fresh page");
  }

  // Test 12: Wider-than-viewport image gets xPos = 0
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo wide = {600, 300};  // wider than VP_WIDTH=480
    engine.addImage(wide);

    runner.expectEq(0, engine.currentPage->elements[0].xPos, "wide_image: xPos clamped to 0");
  }

  // Test 13: Tall image threshold is exclusive (exactly half = not tall)
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo exactHalf = {400, VP_HEIGHT / 2};  // exactly 400, not > 400
    engine.addImage(exactHalf);

    runner.expectEq(0, (int)engine.completedPages.size(), "half_height: not treated as tall");
    runner.expectEq(0, engine.currentPage->elements[0].yPos, "half_height: placed at current position");
  }

  // Test 14: Image just over half is treated as tall
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    ImageInfo justOver = {400, VP_HEIGHT / 2 + 1};  // 401 > 400
    engine.addImage(justOver);

    runner.expectEq(1, (int)engine.completedPages.size(), "just_over_half: treated as tall");
    int expectedY = (VP_HEIGHT - (VP_HEIGHT / 2 + 1)) / 2;
    runner.expectEq(expectedY, engine.completedPages[0]->elements[0].yPos,
                    "just_over_half: vertically centered");
  }

  // Test 15: Small image after tall image + text goes on correct page
  {
    ImageLayoutEngine engine(VP_WIDTH, VP_HEIGHT, LINE_HEIGHT);
    engine.addTextBlock(100);
    ImageInfo tall = {400, 500};
    engine.addImage(tall);
    ImageInfo small = {200, 100};
    engine.addImage(small);

    // text page flushed, tall image page completed, small image on current page
    runner.expectEq(2, (int)engine.completedPages.size(), "mixed_sequence: two completed pages");
    runner.expectEq(1, (int)engine.currentPage->elements.size(), "mixed_sequence: small image on current page");
    runner.expectEq(0, engine.currentPage->elements[0].yPos, "mixed_sequence: small image at top");
  }

  return runner.allPassed() ? 0 : 1;
}
