#include "test_utils.h"

#include <cstdint>
#include <cstring>

#include "Types.h"

int main() {
  TestUtils::TestRunner runner("LongPathBuffer");

  // === ModeTransition.bookPath is now 512 bytes ===

  {
    char bookPath[512] = {};

    // The exact path from issue #106 (~202 bytes)
    const char* longPath =
        "/\xd0\x9a\xd0\xbd\xd0\xb8\xd0\xb3\xd0\xb8"
        "/[\xd0\x9d\xd0\xbe\xd0\xb2\xd0\xbe\xd0\xb5]"
        "/\xd0\x92\xd1\x82\xd0\xbe\xd1\x80\xd0\xb0\xd1\x8f"
        " \xd0\xb6\xd0\xb8\xd0\xb7\xd0\xbd\xd1\x8c"
        " \xd0\xb8\xd0\xb7\xd0\xb2\xd0\xb5\xd1\x81\xd1\x82\xd0\xbd\xd1\x8b\xd1\x85"
        " \xd0\xba\xd0\xbd\xd0\xb8\xd0\xb3, \xd0\xb8\xd0\xbb\xd0\xb8"
        " \xd0\xb8\xd1\x81\xd1\x82\xd0\xbe\xd1\x80\xd0\xb8\xd0\xb8"
        " \xd0\xbf\xd1\x80\xd0\xbe"
        "/\xd0\x9f\xd1\x80\xd0\xbe"
        " \xd0\xa0\xd0\xbe\xd0\xb1\xd0\xb8\xd0\xbd\xd0\xb0"
        " \xd0\x93\xd1\x83\xd0\xb4\xd0\xb0"
        "/\xd0\x9d\xd0\xb0"
        " \xd0\xb0\xd0\xbd\xd0\xb3\xd0\xbb\xd0\xb8\xd0\xb9\xd1\x81\xd0\xba\xd0\xbe\xd0\xbc"
        " \xd1\x8f\xd0\xb7\xd1\x8b\xd0\xba\xd0\xb5"
        "/Nicky Raven - Robin Hood.epub";

    size_t pathLen = strlen(longPath);

    // Simulate the strncpy used in detectBootMode() and saveTransition()
    strncpy(bookPath, longPath, sizeof(bookPath) - 1);
    bookPath[sizeof(bookPath) - 1] = '\0';

    runner.expectTrue(pathLen > 200, "path exceeds old 200-byte bookPath limit");
    runner.expectTrue(pathLen < 512, "path fits in new 512-byte bookPath");
    runner.expectEqual(longPath, bookPath, "full path preserved in 512-byte buffer");

    // Verify extension is intact
    const char* ext = strrchr(bookPath, '.');
    runner.expectTrue(ext != nullptr, "extension dot found");
    runner.expectEqual(".epub", ext, "extension preserved");
  }

  // === Old bookPath[200] would have truncated ===

  {
    char oldBookPath[200] = {};
    const char* longPath =
        "/\xd0\x9a\xd0\xbd\xd0\xb8\xd0\xb3\xd0\xb8"
        "/[\xd0\x9d\xd0\xbe\xd0\xb2\xd0\xbe\xd0\xb5]"
        "/\xd0\x92\xd1\x82\xd0\xbe\xd1\x80\xd0\xb0\xd1\x8f"
        " \xd0\xb6\xd0\xb8\xd0\xb7\xd0\xbd\xd1\x8c"
        " \xd0\xb8\xd0\xb7\xd0\xb2\xd0\xb5\xd1\x81\xd1\x82\xd0\xbd\xd1\x8b\xd1\x85"
        " \xd0\xba\xd0\xbd\xd0\xb8\xd0\xb3, \xd0\xb8\xd0\xbb\xd0\xb8"
        " \xd0\xb8\xd1\x81\xd1\x82\xd0\xbe\xd1\x80\xd0\xb8\xd0\xb8"
        " \xd0\xbf\xd1\x80\xd0\xbe"
        "/\xd0\x9f\xd1\x80\xd0\xbe"
        " \xd0\xa0\xd0\xbe\xd0\xb1\xd0\xb8\xd0\xbd\xd0\xb0"
        " \xd0\x93\xd1\x83\xd0\xb4\xd0\xb0"
        "/\xd0\x9d\xd0\xb0"
        " \xd0\xb0\xd0\xbd\xd0\xb3\xd0\xbb\xd0\xb8\xd0\xb9\xd1\x81\xd0\xba\xd0\xbe\xd0\xbc"
        " \xd1\x8f\xd0\xb7\xd1\x8b\xd0\xba\xd0\xb5"
        "/Nicky Raven - Robin Hood.epub";

    strncpy(oldBookPath, longPath, sizeof(oldBookPath) - 1);
    oldBookPath[sizeof(oldBookPath) - 1] = '\0';

    // Old buffer truncates — path != original
    runner.expectTrue(strcmp(oldBookPath, longPath) != 0, "old 200-byte buffer truncates path");
    runner.expectTrue(strlen(oldBookPath) == 199, "truncated to exactly 199 chars");

    // Extension lost
    const char* ext = strrchr(oldBookPath, '.');
    runner.expectTrue(ext == nullptr || strcmp(ext, ".epub") != 0, "old buffer loses .epub extension");
  }

  // === Path chain: selectedPath → lastBookPath → bookPath → buf.path → contentPath ===

  {
    // Simulate the full path flow with new buffer sizes
    char selectedPath[512] = {};
    char lastBookPath[512] = {};
    char bookPath[512] = {};
    char bufPath[512] = {};
    char contentPath[512] = {};

    // Build path: currentDir + "/" + filename
    char currentDir[512] = {};
    const char* dir =
        "/\xd0\x9a\xd0\xbd\xd0\xb8\xd0\xb3\xd0\xb8"
        "/[\xd0\x9d\xd0\xbe\xd0\xb2\xd0\xbe\xd0\xb5]"
        "/\xd0\x92\xd1\x82\xd0\xbe\xd1\x80\xd0\xb0\xd1\x8f"
        " \xd0\xb6\xd0\xb8\xd0\xb7\xd0\xbd\xd1\x8c"
        " \xd0\xb8\xd0\xb7\xd0\xb2\xd0\xb5\xd1\x81\xd1\x82\xd0\xbd\xd1\x8b\xd1\x85"
        " \xd0\xba\xd0\xbd\xd0\xb8\xd0\xb3, \xd0\xb8\xd0\xbb\xd0\xb8"
        " \xd0\xb8\xd1\x81\xd1\x82\xd0\xbe\xd1\x80\xd0\xb8\xd0\xb8"
        " \xd0\xbf\xd1\x80\xd0\xbe"
        "/\xd0\x9f\xd1\x80\xd0\xbe"
        " \xd0\xa0\xd0\xbe\xd0\xb1\xd0\xb8\xd0\xbd\xd0\xb0"
        " \xd0\x93\xd1\x83\xd0\xb4\xd0\xb0"
        "/\xd0\x9d\xd0\xb0"
        " \xd0\xb0\xd0\xbd\xd0\xb3\xd0\xbb\xd0\xb8\xd0\xb9\xd1\x81\xd0\xba\xd0\xbe\xd0\xbc"
        " \xd1\x8f\xd0\xb7\xd1\x8b\xd0\xba\xd0\xb5";
    const char* filename = "Roger Lancelyn Green - The Adventures of Robin Hood.epub";

    strncpy(currentDir, dir, sizeof(currentDir) - 1);

    // Step 1: FileListState::openSelected builds selectedPath
    snprintf(selectedPath, sizeof(selectedPath), "%s/%s", currentDir, filename);

    // Step 2: saveTransition copies to lastBookPath
    strncpy(lastBookPath, selectedPath, sizeof(lastBookPath) - 1);
    lastBookPath[sizeof(lastBookPath) - 1] = '\0';

    // Step 3: detectBootMode copies to bookPath
    strncpy(bookPath, lastBookPath, sizeof(bookPath) - 1);
    bookPath[sizeof(bookPath) - 1] = '\0';

    // Step 4: initReaderMode copies to buf.path
    strncpy(bufPath, bookPath, sizeof(bufPath) - 1);
    bufPath[sizeof(bufPath) - 1] = '\0';

    // Step 5: ReaderState copies to contentPath
    strncpy(contentPath, bufPath, sizeof(contentPath) - 1);
    contentPath[sizeof(contentPath) - 1] = '\0';

    // All buffers should have the same complete path
    runner.expectEqual(selectedPath, contentPath, "path survives full chain without truncation");

    // Extension must be intact at the end
    const char* ext = strrchr(contentPath, '.');
    runner.expectTrue(ext != nullptr, "extension dot found in contentPath");
    runner.expectEqual(".epub", ext, "extension intact after full chain");

    // Path length sanity
    size_t pathLen = strlen(contentPath);
    runner.expectTrue(pathLen > 200, "full chain path > 200 bytes");
    runner.expectTrue(pathLen < 512, "full chain path < 512 bytes");
  }

  // === Buffer sizes match Types.h constants ===

  {
    runner.expectEq(size_t(papyrix::BufferSize::FilePath), size_t(512), "FilePath buffer size is 512");
    runner.expectEq(size_t(papyrix::BufferSize::Path), size_t(256), "Path buffer size is 256 (for cache paths)");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
