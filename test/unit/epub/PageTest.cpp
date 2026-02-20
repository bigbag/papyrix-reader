// Unit tests for Page::hasImages() and Page::getImageBoundingBox()

#include "test_utils.h"

#include <memory>
#include <vector>

#include "Page.h"

// Link stubs for virtual methods we don't exercise
void PageLine::render(GfxRenderer&, int, int, int, bool) {}
bool PageLine::serialize(FsFile&) { return false; }
std::unique_ptr<PageLine> PageLine::deserialize(FsFile&) { return nullptr; }

void PageImage::render(GfxRenderer&, int, int, int, bool) {}
bool PageImage::serialize(FsFile&) { return false; }
std::unique_ptr<PageImage> PageImage::deserialize(FsFile&) { return nullptr; }

void Page::render(GfxRenderer&, int, int, int, bool) const {}
bool Page::serialize(FsFile&) const { return false; }
std::unique_ptr<Page> Page::deserialize(FsFile&) { return nullptr; }

void ImageBlock::render(GfxRenderer&, int, int, int) const {}
bool ImageBlock::serialize(FsFile&) const { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile&) { return nullptr; }

void TextBlock::render(const GfxRenderer&, int, int, int, bool) const {}
bool TextBlock::serialize(FsFile&) const { return false; }
std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile&) { return nullptr; }

static std::shared_ptr<PageImage> makeImage(int16_t x, int16_t y, uint16_t w, uint16_t h) {
  auto block = std::make_shared<ImageBlock>("img.bmp", w, h);
  return std::make_shared<PageImage>(block, x, y);
}

static std::shared_ptr<PageLine> makeLine(int16_t x, int16_t y) {
  std::vector<TextBlock::WordData> words = {{"hello", 0, {}}};
  auto block = std::make_shared<TextBlock>(std::move(words), TextBlock::JUSTIFIED);
  return std::make_shared<PageLine>(block, x, y);
}

int main() {
  TestUtils::TestRunner runner("Page");

  // --- hasImages() tests ---

  {
    Page page;
    runner.expectFalse(page.hasImages(), "empty page has no images");
  }

  {
    Page page;
    page.elements.push_back(makeLine(0, 0));
    page.elements.push_back(makeLine(0, 20));
    runner.expectFalse(page.hasImages(), "text-only page has no images");
  }

  {
    Page page;
    page.elements.push_back(makeImage(0, 0, 100, 50));
    runner.expectTrue(page.hasImages(), "single image detected");
  }

  {
    Page page;
    page.elements.push_back(makeLine(0, 0));
    page.elements.push_back(makeImage(0, 20, 100, 50));
    page.elements.push_back(makeLine(0, 70));
    runner.expectTrue(page.hasImages(), "mixed text and images detected");
  }

  // --- getImageBoundingBox() tests ---

  {
    Page page;
    int16_t x = -1, y = -1, w = -1, h = -1;
    runner.expectFalse(page.getImageBoundingBox(x, y, w, h), "empty page returns false");
    runner.expectEq(-1, (int)x, "empty page: x unchanged");
    runner.expectEq(-1, (int)y, "empty page: y unchanged");
  }

  {
    Page page;
    page.elements.push_back(makeLine(0, 0));
    int16_t x = -1, y = -1, w = -1, h = -1;
    runner.expectFalse(page.getImageBoundingBox(x, y, w, h), "text-only returns false");
  }

  {
    Page page;
    page.elements.push_back(makeImage(10, 20, 100, 50));
    int16_t x, y, w, h;
    runner.expectTrue(page.getImageBoundingBox(x, y, w, h), "single image returns true");
    runner.expectEq(10, (int)x, "single image: x");
    runner.expectEq(20, (int)y, "single image: y");
    runner.expectEq(100, (int)w, "single image: w");
    runner.expectEq(50, (int)h, "single image: h");
  }

  {
    // Two images: (10,20,100,50) and (50,100,200,80)
    // Union: x=10, y=20, right=250, bottom=180 → w=240, h=160
    Page page;
    page.elements.push_back(makeImage(10, 20, 100, 50));
    page.elements.push_back(makeImage(50, 100, 200, 80));
    int16_t x, y, w, h;
    runner.expectTrue(page.getImageBoundingBox(x, y, w, h), "multi image returns true");
    runner.expectEq(10, (int)x, "multi image: x = min(10,50)");
    runner.expectEq(20, (int)y, "multi image: y = min(20,100)");
    runner.expectEq(240, (int)w, "multi image: w = 250-10");
    runner.expectEq(160, (int)h, "multi image: h = 180-20");
  }

  {
    Page page;
    page.elements.push_back(makeImage(0, 0, 64, 64));
    int16_t x, y, w, h;
    runner.expectTrue(page.getImageBoundingBox(x, y, w, h), "origin image returns true");
    runner.expectEq(0, (int)x, "origin image: x");
    runner.expectEq(0, (int)y, "origin image: y");
    runner.expectEq(64, (int)w, "origin image: w");
    runner.expectEq(64, (int)h, "origin image: h");
  }

  {
    // Mix text and images, only images contribute to bounding box
    Page page;
    page.elements.push_back(makeLine(0, 0));
    page.elements.push_back(makeImage(30, 40, 80, 60));
    page.elements.push_back(makeLine(0, 200));
    page.elements.push_back(makeImage(100, 10, 50, 120));
    int16_t x, y, w, h;
    runner.expectTrue(page.getImageBoundingBox(x, y, w, h), "mixed returns true");
    // img1: (30,40)→(110,100), img2: (100,10)→(150,130)
    // union: x=30, y=10, right=150, bottom=130 → w=120, h=120
    runner.expectEq(30, (int)x, "mixed: x = min(30,100)");
    runner.expectEq(10, (int)y, "mixed: y = min(40,10)");
    runner.expectEq(120, (int)w, "mixed: w = 150-30");
    runner.expectEq(120, (int)h, "mixed: h = 130-10");
  }

  return runner.allPassed() ? 0 : 1;
}
