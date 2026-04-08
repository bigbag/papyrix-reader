// Base64 streaming decoder unit tests

#include "test_utils.h"

#include "Base64Decoder.h"

#include <cstdint>
#include <string>
#include <vector>

static std::string decode(const std::string& input) {
  Base64Decoder decoder;
  std::string result;
  decoder.feed(input.c_str(), static_cast<int>(input.size()), [&result](const uint8_t* data, size_t len) {
    result.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  decoder.finish([&result](const uint8_t* data, size_t len) {
    result.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  return result;
}

int main() {
  TestUtils::TestRunner runner("Fb2 Base64 Decoder");

  // Basic decoding
  {
    runner.expectEqual("Hello", decode("SGVsbG8="), "basic: 'Hello'");
    runner.expectEqual("Hello!", decode("SGVsbG8h"), "no_padding: 'Hello!'");
    runner.expectEqual("Hi", decode("SGk="), "two_byte: 'Hi'");
    runner.expectEqual("A", decode("QQ=="), "one_byte: 'A'");
  }

  // Empty input
  {
    runner.expectEqual("", decode(""), "empty: empty input");
  }

  // Whitespace in input (typical FB2 base64 formatting)
  {
    runner.expectEqual("Hello", decode("SGVs\nbG8="), "newline: decoded through newline");
    runner.expectEqual("Hello", decode("SGVs\r\nbG8="), "crlf: decoded through CRLF");
    runner.expectEqual("Hello", decode("  SGVs  bG8=  "), "spaces: decoded through spaces");
    runner.expectEqual("Hello", decode("\tSGVs\tbG8=\t"), "tabs: decoded through tabs");
  }

  // Longer text
  {
    std::string expected = "The quick brown fox jumps over the lazy dog";
    runner.expectEqual(expected, decode("VGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZw=="),
                       "sentence: long text");
  }

  // Binary data (non-ASCII)
  {
    Base64Decoder decoder;
    std::vector<uint8_t> result;
    std::string input = "//79/A==";
    decoder.feed(input.c_str(), static_cast<int>(input.size()), [&result](const uint8_t* data, size_t len) {
      result.insert(result.end(), data, data + len);
      return true;
    });
    decoder.finish([&result](const uint8_t* data, size_t len) {
      result.insert(result.end(), data, data + len);
      return true;
    });
    runner.expectEq(static_cast<size_t>(4), result.size(), "binary: 4 bytes");
    runner.expectEq(static_cast<uint8_t>(0xFF), result[0], "binary: byte 0");
    runner.expectEq(static_cast<uint8_t>(0xFE), result[1], "binary: byte 1");
    runner.expectEq(static_cast<uint8_t>(0xFD), result[2], "binary: byte 2");
    runner.expectEq(static_cast<uint8_t>(0xFC), result[3], "binary: byte 3");
  }

  // Chunked feeding (simulates Expat character data callback)
  {
    Base64Decoder decoder;
    std::string result;
    auto write = [&result](const uint8_t* data, size_t len) {
      result.append(reinterpret_cast<const char*>(data), len);
      return true;
    };
    // Feed "SGVsbG8=" one character at a time
    std::string input = "SGVsbG8=";
    for (size_t i = 0; i < input.size(); i++) {
      decoder.feed(&input[i], 1, write);
    }
    decoder.finish(write);
    runner.expectEqual("Hello", result, "chunked: byte-by-byte feeding");
  }

  // Large input (test buffer flushing)
  {
    // Create input that's larger than the internal buffer (256 chars)
    std::string largeInput;
    for (int i = 0; i < 2000; i++) {
      largeInput += "QUFB";  // "AAA" repeated
    }
    Base64Decoder decoder;
    std::string result;
    decoder.feed(largeInput.c_str(), static_cast<int>(largeInput.size()), [&result](const uint8_t* data, size_t len) {
      result.append(reinterpret_cast<const char*>(data), len);
      return true;
    });
    decoder.finish([&result](const uint8_t* data, size_t len) {
      result.append(reinterpret_cast<const char*>(data), len);
      return true;
    });
    runner.expectEq(static_cast<size_t>(6000), result.size(), "large: 6000 bytes output");
    runner.expectTrue(result[0] == 'A' && result[5999] == 'A', "large: all A's");
  }

  // Truncated input (not a multiple of 4) should fail
  {
    Base64Decoder decoder;
    std::string result;
    std::string input = "SGVsbG8";  // "SGVsbG8=" without padding
    decoder.feed(input.c_str(), static_cast<int>(input.size()), [&result](const uint8_t* data, size_t len) {
      result.append(reinterpret_cast<const char*>(data), len);
      return true;
    });
    bool ok = decoder.finish([&result](const uint8_t* data, size_t len) {
      result.append(reinterpret_cast<const char*>(data), len);
      return true;
    });
    runner.expectTrue(!ok, "truncated: finish returns false");
    runner.expectTrue(decoder.failed(), "truncated: decoder reports failure");
  }

  // Invalid padding ("xx=A" instead of "xx==")
  {
    Base64Decoder decoder;
    std::string result;
    auto write = [&result](const uint8_t* data, size_t len) {
      result.append(reinterpret_cast<const char*>(data), len);
      return true;
    };
    std::string input = "QQ=A";  // Should be "QQ==" for 'A'
    decoder.feed(input.c_str(), static_cast<int>(input.size()), write);
    bool ok = decoder.finish(write);
    runner.expectTrue(!ok, "bad_padding: finish returns false");
    runner.expectTrue(decoder.failed(), "bad_padding: decoder rejects xx=A");
  }

  // Write callback failure
  {
    Base64Decoder decoder;
    // Use input larger than internal buffer (256) to trigger flush during feed
    std::string input;
    for (int i = 0; i < 1500; i++) input += "QUFB";  // 6000 chars > 256 buffer
    decoder.feed(input.c_str(), static_cast<int>(input.size()), [](const uint8_t*, size_t) {
      return false;  // Simulate write failure
    });
    runner.expectTrue(decoder.failed(), "write_fail: decoder reports failure");
  }

  return runner.allPassed() ? 0 : 1;
}
