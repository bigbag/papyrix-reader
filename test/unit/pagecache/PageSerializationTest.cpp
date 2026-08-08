#include "test_utils.h"

#include <string>

#include "HardwareSerial.h"
#include "Page.h"
#include "SdFat.h"
#include "Serialization.h"

void ImageBlock::render(GfxRenderer&, int, int, int) const {}

bool ImageBlock::serialize(FsFile&) const { return false; }

std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile&) { return nullptr; }

int main() {
  TestUtils::TestRunner runner("PageSerialization");

  {
    FsFile file;
    file.setBuffer("");
    runner.expectTrue(Page::deserialize(file) == nullptr, "empty_page_rejected");
  }

  {
    FsFile file;
    file.setBuffer(std::string("\x01\x00\x01", 3));
    runner.expectTrue(Page::deserialize(file) == nullptr, "truncated_line_coordinates_rejected");
  }

  {
    FsFile file;
    file.setBuffer(std::string("\x01\x00\x02", 3));
    runner.expectTrue(Page::deserialize(file) == nullptr, "truncated_image_coordinates_rejected");
  }

  {
    Page page;
    FsFile file;
    file.setBuffer("");
    file.setWriteLimit(1);
    runner.expectFalse(page.serialize(file), "short_page_count_write_rejected");
  }

  {
    Page page;
    FsFile file;
    file.setBuffer("");
    runner.expectTrue(page.serialize(file), "empty_page_serialize_success");
    file.seek(0);
    auto decoded = Page::deserialize(file);
    runner.expectTrue(decoded != nullptr, "empty_page_roundtrip_success");
    runner.expectEq<size_t>(0, decoded ? decoded->elements.size() : 1, "empty_page_roundtrip_count");
  }

  return runner.allPassed() ? 0 : 1;
}
