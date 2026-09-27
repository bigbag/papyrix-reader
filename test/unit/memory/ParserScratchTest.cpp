#include "test_utils.h"

#include <BuildArena.h>
#include <ParserScratch.h>
#include <platform_stubs.h>

#include <array>
#include <cstdint>
#include <cstring>

int main() {
  TestUtils::TestRunner runner("ParserScratch");
  std::array<uint8_t, 48000> framebuffer;
  framebuffer.fill(0x7B);
  testSetHeapPool(MALLOC_CAP_INTERNAL, 4, 4);
  testSetHeapPool(MALLOC_CAP_SPIRAM, 128 * 1024, 128 * 1024);
  const size_t freesBefore = testHeapCapsFreeCount();
  {
    ParserScratch storage(framebuffer.data(), framebuffer.size());
    BuildArena arena(storage.data(), storage.capacity());
    uint8_t* dictionary = arena.allocArray<uint8_t>(32 * 1024);
    if (dictionary) memset(dictionary, 0x13, 32 * 1024);
    runner.expectTrue(dictionary && storage.data() != framebuffer.data() && arena.capacity() >= 64 * 1024 &&
                          framebuffer[0] == 0x7B && framebuffer.back() == 0x7B,
                      "S3 parser writes leave both ends of framebuffer intact");
  }
  runner.expectEq<size_t>(freesBefore + 1, testHeapCapsFreeCount(), "owned parser scratch frees its PSRAM buffer");

  testSetPsramAllocationFailure(true);
  {
    ParserScratch storage(framebuffer.data(), framebuffer.size());
    BuildArena arena(storage.data(), storage.capacity());
    uint8_t* dictionary = arena.allocArray<uint8_t>(32 * 1024);
    if (dictionary) memset(dictionary, 0x21, 32 * 1024);
    runner.expectTrue(dictionary && storage.data() == framebuffer.data() && framebuffer[0] == 0x21 &&
                          arena.capacity() == framebuffer.size(),
                      "PSRAM failure retains serialized borrowed scratch");
  }
  runner.expectEq<size_t>(freesBefore + 1, testHeapCapsFreeCount(), "allocation failure does not free framebuffer");
  testSetPsramAllocationFailure(false);
  testSetHeapPool(MALLOC_CAP_SPIRAM, 60 * 1024, 60 * 1024);
  {
    ParserScratch storage(framebuffer.data(), framebuffer.size());
    runner.expectTrue(storage.data() == framebuffer.data() && storage.capacity() == framebuffer.size(),
                      "insufficient PSRAM headroom borrows framebuffer");
  }
  runner.expectEq<size_t>(freesBefore + 1, testHeapCapsFreeCount(), "headroom refusal does not free framebuffer");
  testResetLargestFreeBlock();
  return runner.allPassed() ? 0 : 1;
}
