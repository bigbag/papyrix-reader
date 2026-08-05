#include "test_utils.h"

#include <InflateReader.h>

#include <cstring>
#include <string>

// clang-format off
// Raw deflate (no zlib header) of "Hello, World!" (13 bytes)
static const uint8_t kHelloDeflated[] = {
  0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0x08,
  0xcf, 0x2f, 0xca, 0x49, 0x51, 0x04, 0x00
};
static constexpr size_t kHelloDeflatedSize = sizeof(kHelloDeflated);
static constexpr size_t kHelloInflatedSize = 13;

// Raw deflate of "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" x 20 (1040 bytes)
static const uint8_t kLargeDeflated[] = {
  0x4b, 0x4c, 0x4a, 0x4e, 0x49, 0x4d, 0x4b, 0xcf,
  0xc8, 0xcc, 0xca, 0xce, 0xc9, 0xcd, 0xcb, 0x2f,
  0x28, 0x2c, 0x2a, 0x2e, 0x29, 0x2d, 0x2b, 0xaf,
  0xa8, 0xac, 0x72, 0x74, 0x72, 0x76, 0x71, 0x75,
  0x73, 0xf7, 0xf0, 0xf4, 0xf2, 0xf6, 0xf1, 0xf5,
  0xf3, 0x0f, 0x08, 0x0c, 0x0a, 0x0e, 0x09, 0x0d,
  0x0b, 0x8f, 0x88, 0x8c, 0x4a, 0x1c, 0xd5, 0x33,
  0xaa, 0x67, 0x54, 0xcf, 0xb0, 0xd4, 0x03, 0x00,
};
static constexpr size_t kLargeDeflatedSize = sizeof(kLargeDeflated);
static constexpr size_t kLargeInflatedSize = 1040;
// clang-format on

static std::string makeExpectedLarge() {
  const std::string letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::string result;
  result.reserve(kLargeInflatedSize);
  for (int i = 0; i < 20; i++) result += letters;
  return result;
}

// Streaming callback context: feeds compressed data in small chunks.
struct ChunkedCtx {
  InflateReader reader;  // must be first member
  const uint8_t* src;
  size_t remaining;
  uint8_t buf[16];  // small buffer to force multiple refills
};

static int chunkedReadCb(uzlib_uncomp* uncomp) {
  auto* ctx = reinterpret_cast<ChunkedCtx*>(uncomp);
  if (ctx->remaining == 0) return -1;

  size_t toRead = ctx->remaining < sizeof(ctx->buf) ? ctx->remaining : sizeof(ctx->buf);
  memcpy(ctx->buf, ctx->src, toRead);
  ctx->src += toRead;
  ctx->remaining -= toRead;

  uncomp->source = ctx->buf + 1;
  uncomp->source_limit = ctx->buf + toRead;
  return ctx->buf[0];
}

int main() {
  TestUtils::TestRunner runner("InflateReader");
  runner.expectEq<size_t>(32768, InflateReader::STREAMING_DICTIONARY_SIZE,
                          "streaming dictionary contract remains 32 KiB");

  // ---- One-shot mode: read() ----
  {
    InflateReader r;
    runner.expectTrue(r.init(false), "one-shot init succeeds");

    r.setSource(kHelloDeflated, kHelloDeflatedSize);
    uint8_t out[kHelloInflatedSize] = {};
    bool ok = r.read(out, kHelloInflatedSize);
    runner.expectTrue(ok, "one-shot read: Hello, World!");
    runner.expectTrue(memcmp(out, "Hello, World!", kHelloInflatedSize) == 0,
                      "one-shot read: output matches");
  }

  // ---- One-shot mode: larger data ----
  {
    InflateReader r;
    r.init(false);
    r.setSource(kLargeDeflated, kLargeDeflatedSize);

    uint8_t out[kLargeInflatedSize] = {};
    bool ok = r.read(out, kLargeInflatedSize);
    runner.expectTrue(ok, "one-shot read: large data succeeds");

    std::string expected = makeExpectedLarge();
    runner.expectTrue(memcmp(out, expected.data(), kLargeInflatedSize) == 0,
                      "one-shot read: large data matches");
  }

  // ---- Streaming mode with readAtMost() ----
  {
    InflateReader r;
    runner.expectTrue(r.init(true), "streaming init succeeds (allocates ring buffer)");

    r.setSource(kLargeDeflated, kLargeDeflatedSize);

    std::string result;
    uint8_t chunk[64];  // small output buffer to force multiple iterations
    bool done = false;
    bool error = false;

    while (!done) {
      size_t produced = 0;
      InflateStatus status = r.readAtMost(chunk, sizeof(chunk), &produced);
      result.append(reinterpret_cast<const char*>(chunk), produced);

      if (status == InflateStatus::Done) {
        done = true;
      } else if (status == InflateStatus::Error) {
        error = true;
        done = true;
      }
    }

    runner.expectFalse(error, "streaming readAtMost: no error");
    runner.expectEq(kLargeInflatedSize, result.size(), "streaming readAtMost: correct size");

    std::string expected = makeExpectedLarge();
    runner.expectTrue(result == expected, "streaming readAtMost: output matches");
  }

  // ---- Streaming mode with external buffer ----
  {
    uint8_t externalBuf[32768] = {};
    InflateReader r;
    runner.expectTrue(r.init(true, externalBuf), "streaming init with external buffer");

    r.setSource(kLargeDeflated, kLargeDeflatedSize);

    uint8_t out[kLargeInflatedSize] = {};
    bool ok = r.read(out, kLargeInflatedSize);
    runner.expectTrue(ok, "streaming with external buffer: read succeeds");

    std::string expected = makeExpectedLarge();
    runner.expectTrue(memcmp(out, expected.data(), kLargeInflatedSize) == 0,
                      "streaming with external buffer: output matches");
  }

  // ---- Streaming with read callback (chunked input) ----
  {
    ChunkedCtx ctx = {};
    ctx.src = kLargeDeflated;
    ctx.remaining = kLargeDeflatedSize;

    runner.expectTrue(ctx.reader.init(true), "callback streaming init");
    ctx.reader.setReadCallback(chunkedReadCb);

    std::string result;
    uint8_t chunk[128];
    bool done = false;
    bool error = false;

    while (!done) {
      size_t produced = 0;
      InflateStatus status = ctx.reader.readAtMost(chunk, sizeof(chunk), &produced);
      result.append(reinterpret_cast<const char*>(chunk), produced);

      if (status == InflateStatus::Done) {
        done = true;
      } else if (status == InflateStatus::Error) {
        error = true;
        done = true;
      }
    }

    runner.expectFalse(error, "callback streaming: no error");
    runner.expectEq(kLargeInflatedSize, result.size(), "callback streaming: correct size");

    std::string expected = makeExpectedLarge();
    runner.expectTrue(result == expected, "callback streaming: output matches");
  }

  // ---- Error: corrupt data ----
  {
    InflateReader r;
    r.init(false);

    uint8_t corrupt[] = {0xFF, 0xFF, 0xFF, 0xFF};
    r.setSource(corrupt, sizeof(corrupt));

    uint8_t out[64] = {};
    bool ok = r.read(out, sizeof(out));
    runner.expectFalse(ok, "corrupt data: read returns false");
  }

  // ---- deinit and reinit ----
  {
    InflateReader r;
    r.init(true);
    r.deinit();

    // Reinit in one-shot mode after streaming init
    r.init(false);
    r.setSource(kHelloDeflated, kHelloDeflatedSize);

    uint8_t out[kHelloInflatedSize] = {};
    bool ok = r.read(out, kHelloInflatedSize);
    runner.expectTrue(ok, "deinit/reinit: read succeeds after mode switch");
    runner.expectTrue(memcmp(out, "Hello, World!", kHelloInflatedSize) == 0,
                      "deinit/reinit: output matches");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
