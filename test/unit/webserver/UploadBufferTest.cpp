// Tests for the buffered write algorithm used in PapyrixWebServer upload handling.
// The core logic: data arrives in chunks, gets accumulated in a 4KB buffer,
// and flushed to disk when full. This avoids many small SD card writes.

#include "test_utils.h"

#include <cstring>
#include <vector>

#include "SdFat.h"

namespace {

static constexpr size_t BUFFER_SIZE = 4096;

struct UploadBuffer {
  FsFile file;
  std::vector<uint8_t> buffer;
  size_t bufferPos = 0;
  bool failNextWrite = false;

  void init() {
    file.setBuffer("");
    buffer.resize(BUFFER_SIZE);
    bufferPos = 0;
  }
};

// Mirrors PapyrixWebServer::flushUploadBuffer()
bool flushBuffer(UploadBuffer& state) {
  if (state.bufferPos > 0 && state.file) {
    if (state.failNextWrite) {
      state.bufferPos = 0;
      return false;
    }
    const size_t written = state.file.write(state.buffer.data(), state.bufferPos);
    if (written != state.bufferPos) {
      state.bufferPos = 0;
      return false;
    }
    state.bufferPos = 0;
  }
  return true;
}

// Mirrors the UPLOAD_FILE_WRITE loop in handleUpload()
bool writeChunk(UploadBuffer& state, const uint8_t* data, size_t size) {
  size_t remaining = size;

  while (remaining > 0) {
    size_t space = BUFFER_SIZE - state.bufferPos;
    size_t toCopy = remaining < space ? remaining : space;
    memcpy(state.buffer.data() + state.bufferPos, data, toCopy);
    state.bufferPos += toCopy;
    data += toCopy;
    remaining -= toCopy;

    if (state.bufferPos >= BUFFER_SIZE) {
      if (!flushBuffer(state)) {
        return false;
      }
    }
  }
  return true;
}

// Helper: create a data pattern of given size
std::vector<uint8_t> makeData(size_t size) {
  std::vector<uint8_t> data(size);
  for (size_t i = 0; i < size; i++) {
    data[i] = static_cast<uint8_t>(i & 0xFF);
  }
  return data;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("Upload Buffer");

  // --- flushBuffer basic behavior ---

  // Flush with empty buffer is a no-op success
  {
    UploadBuffer state;
    state.init();
    bool ok = flushBuffer(state);
    runner.expectTrue(ok, "Flush empty buffer succeeds");
    runner.expectEq(std::string(""), state.file.getBuffer(), "Flush empty: nothing written to file");
  }

  // Flush with data writes to file and resets bufferPos
  {
    UploadBuffer state;
    state.init();
    state.buffer[0] = 0xAA;
    state.buffer[1] = 0xBB;
    state.bufferPos = 2;

    bool ok = flushBuffer(state);
    runner.expectTrue(ok, "Flush with data succeeds");
    runner.expectEq(static_cast<size_t>(0), state.bufferPos, "Flush resets bufferPos");
    runner.expectEq(static_cast<size_t>(2), state.file.getBuffer().size(), "Flush writes correct size");
  }

  // Flush failure resets bufferPos and returns false
  {
    UploadBuffer state;
    state.init();
    state.bufferPos = 10;
    state.failNextWrite = true;

    bool ok = flushBuffer(state);
    runner.expectFalse(ok, "Flush failure returns false");
    runner.expectEq(static_cast<size_t>(0), state.bufferPos, "Flush failure resets bufferPos");
  }

  // Flush on closed file is a no-op success
  {
    UploadBuffer state;
    state.init();
    state.file.close();
    state.bufferPos = 5;

    bool ok = flushBuffer(state);
    runner.expectTrue(ok, "Flush on closed file is no-op success");
    runner.expectEq(static_cast<size_t>(5), state.bufferPos, "Flush on closed file: bufferPos unchanged");
  }

  // --- Buffered write with various chunk sizes ---

  // Write less than buffer size: stays in buffer, not flushed yet
  {
    UploadBuffer state;
    state.init();

    auto data = makeData(100);
    bool ok = writeChunk(state, data.data(), data.size());

    runner.expectTrue(ok, "Small write succeeds");
    runner.expectEq(static_cast<size_t>(100), state.bufferPos, "Small write: bufferPos = 100");
    runner.expectEq(std::string(""), state.file.getBuffer(), "Small write: not yet flushed");

    // Final flush writes to file
    flushBuffer(state);
    runner.expectEq(static_cast<size_t>(100), state.file.getBuffer().size(), "Small write: final flush correct");
  }

  // Write exactly buffer size: triggers one flush
  {
    UploadBuffer state;
    state.init();

    auto data = makeData(BUFFER_SIZE);
    bool ok = writeChunk(state, data.data(), data.size());

    runner.expectTrue(ok, "Exact buffer write succeeds");
    runner.expectEq(static_cast<size_t>(0), state.bufferPos, "Exact: bufferPos reset after flush");
    runner.expectEq(BUFFER_SIZE, state.file.getBuffer().size(), "Exact: one full flush to file");
  }

  // Write slightly more than buffer: one flush + remainder in buffer
  {
    UploadBuffer state;
    state.init();

    size_t dataSize = BUFFER_SIZE + 500;
    auto data = makeData(dataSize);
    bool ok = writeChunk(state, data.data(), data.size());

    runner.expectTrue(ok, "Overflow write succeeds");
    runner.expectEq(static_cast<size_t>(500), state.bufferPos, "Overflow: remainder in buffer");
    runner.expectEq(BUFFER_SIZE, state.file.getBuffer().size(), "Overflow: one flush done");

    // Final flush
    flushBuffer(state);
    runner.expectEq(dataSize, state.file.getBuffer().size(), "Overflow: total data correct after final flush");
  }

  // Write exactly 2x buffer size: two flushes, buffer empty
  {
    UploadBuffer state;
    state.init();

    size_t dataSize = BUFFER_SIZE * 2;
    auto data = makeData(dataSize);
    bool ok = writeChunk(state, data.data(), data.size());

    runner.expectTrue(ok, "2x buffer write succeeds");
    runner.expectEq(static_cast<size_t>(0), state.bufferPos, "2x: bufferPos is 0");
    runner.expectEq(dataSize, state.file.getBuffer().size(), "2x: all data flushed");
  }

  // Multiple small writes accumulate correctly
  {
    UploadBuffer state;
    state.init();

    auto chunk = makeData(1000);
    for (int i = 0; i < 10; i++) {
      writeChunk(state, chunk.data(), chunk.size());
    }

    // 10 * 1000 = 10000 bytes. 10000 / 4096 = 2 full flushes (8192 bytes), 1808 in buffer
    runner.expectEq(static_cast<size_t>(1808), state.bufferPos, "Multiple small: correct remainder");
    runner.expectEq(static_cast<size_t>(8192), state.file.getBuffer().size(), "Multiple small: 2 flushes done");

    flushBuffer(state);
    runner.expectEq(static_cast<size_t>(10000), state.file.getBuffer().size(), "Multiple small: total correct");
  }

  // Single byte writes accumulate
  {
    UploadBuffer state;
    state.init();

    for (size_t i = 0; i < BUFFER_SIZE; i++) {
      uint8_t byte = static_cast<uint8_t>(i & 0xFF);
      writeChunk(state, &byte, 1);
    }

    // Exactly BUFFER_SIZE single-byte writes should trigger one flush
    runner.expectEq(static_cast<size_t>(0), state.bufferPos, "Single bytes: buffer flushed at capacity");
    runner.expectEq(BUFFER_SIZE, state.file.getBuffer().size(), "Single bytes: full buffer flushed");
  }

  // --- Data integrity ---

  // Verify written data matches input exactly
  {
    UploadBuffer state;
    state.init();

    auto data = makeData(BUFFER_SIZE + 100);
    writeChunk(state, data.data(), data.size());
    flushBuffer(state);

    const std::string& written = state.file.getBuffer();
    runner.expectEq(data.size(), written.size(), "Integrity: size matches");

    bool match = true;
    for (size_t i = 0; i < data.size() && i < written.size(); i++) {
      if (data[i] != static_cast<uint8_t>(written[i])) {
        match = false;
        break;
      }
    }
    runner.expectTrue(match, "Integrity: data matches byte-for-byte");
  }

  // Multiple varied-size chunks produce correct output
  {
    UploadBuffer state;
    state.init();

    std::vector<uint8_t> allData;
    size_t chunkSizes[] = {1, 100, 4095, 4096, 4097, 1, 8000, 500};
    uint8_t pattern = 0;

    for (size_t sz : chunkSizes) {
      std::vector<uint8_t> chunk(sz);
      for (size_t i = 0; i < sz; i++) {
        chunk[i] = pattern++;
      }
      allData.insert(allData.end(), chunk.begin(), chunk.end());
      writeChunk(state, chunk.data(), chunk.size());
    }
    flushBuffer(state);

    const std::string& written = state.file.getBuffer();
    runner.expectEq(allData.size(), written.size(), "Varied chunks: total size correct");

    bool match = true;
    for (size_t i = 0; i < allData.size() && i < written.size(); i++) {
      if (allData[i] != static_cast<uint8_t>(written[i])) {
        match = false;
        break;
      }
    }
    runner.expectTrue(match, "Varied chunks: data integrity preserved");
  }

  // --- Write failure during chunk ---

  // Flush failure mid-write aborts the write
  {
    UploadBuffer state;
    state.init();

    // Fill buffer almost full
    auto data = makeData(BUFFER_SIZE - 10);
    writeChunk(state, data.data(), data.size());

    // Now set failure and write enough to trigger flush
    state.failNextWrite = true;
    auto moreData = makeData(100);
    bool ok = writeChunk(state, moreData.data(), moreData.size());

    runner.expectFalse(ok, "Write failure: returns false");
  }

  // --- Zero-length write ---
  {
    UploadBuffer state;
    state.init();

    bool ok = writeChunk(state, nullptr, 0);
    runner.expectTrue(ok, "Zero-length write succeeds");
    runner.expectEq(static_cast<size_t>(0), state.bufferPos, "Zero-length: bufferPos unchanged");
  }

  return runner.allPassed() ? 0 : 1;
}
