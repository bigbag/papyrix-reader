#include "test_utils.h"

#include <cstring>
#include <sstream>
#include <string>

// Include mocks before the library
#include "HardwareSerial.h"
#include "SdFat.h"

// Now include the serialization header
#include "Serialization.h"

int main() {
  TestUtils::TestRunner runner("Serialization Functions");

  // ============================================
  // writePod() / readPod() tests with std::iostream
  // ============================================

  // Test 1: uint8_t roundtrip
  {
    std::stringstream ss;
    uint8_t writeVal = 0xAB;
    serialization::writePod(ss, writeVal);

    uint8_t readVal = 0;
    serialization::readPod(ss, readVal);
    runner.expectEq(writeVal, readVal, "writePod/readPod: uint8_t roundtrip");
  }

  // Test 2: uint16_t roundtrip
  {
    std::stringstream ss;
    uint16_t writeVal = 0x1234;
    serialization::writePod(ss, writeVal);

    uint16_t readVal = 0;
    serialization::readPod(ss, readVal);
    runner.expectEq(writeVal, readVal, "writePod/readPod: uint16_t roundtrip");
  }

  // Test 3: uint32_t roundtrip
  {
    std::stringstream ss;
    uint32_t writeVal = 0xDEADBEEF;
    serialization::writePod(ss, writeVal);

    uint32_t readVal = 0;
    serialization::readPod(ss, readVal);
    runner.expectEq(writeVal, readVal, "writePod/readPod: uint32_t roundtrip");
  }

  // Test 4: float roundtrip
  {
    std::stringstream ss;
    float writeVal = 3.14159f;
    serialization::writePod(ss, writeVal);

    float readVal = 0;
    serialization::readPod(ss, readVal);
    runner.expectFloatEq(writeVal, readVal, "writePod/readPod: float roundtrip");
  }

  // Test 5: struct roundtrip
  {
    struct TestStruct {
      uint32_t a;
      uint16_t b;
      uint8_t c;
    } __attribute__((packed));

    std::stringstream ss;
    TestStruct writeVal = {0x12345678, 0xABCD, 0xEF};
    serialization::writePod(ss, writeVal);

    TestStruct readVal = {0, 0, 0};
    serialization::readPod(ss, readVal);
    runner.expectEq(writeVal.a, readVal.a, "writePod/readPod: struct field a");
    runner.expectEq(writeVal.b, readVal.b, "writePod/readPod: struct field b");
    runner.expectEq(writeVal.c, readVal.c, "writePod/readPod: struct field c");
  }

  // ============================================
  // writePod() / readPod() tests with FsFile
  // ============================================

  // Test 6: FsFile uint32_t roundtrip
  {
    FsFile file;
    file.setBuffer("");  // Start with empty buffer

    uint32_t writeVal = 0x87654321;
    serialization::writePod(file, writeVal);

    file.seek(0);  // Reset to beginning for reading
    uint32_t readVal = 0;
    serialization::readPod(file, readVal);
    runner.expectEq(writeVal, readVal, "writePod/readPod FsFile: uint32_t roundtrip");
  }

  // ============================================
  // readPodChecked() tests
  // ============================================

  // Test 7: readPodChecked success
  {
    FsFile file;
    std::string data(4, '\x00');
    data[0] = 0x12;
    data[1] = 0x34;
    data[2] = 0x56;
    data[3] = 0x78;
    file.setBuffer(data);

    uint32_t val = 0;
    bool success = serialization::readPodChecked(file, val);
    runner.expectTrue(success, "readPodChecked: returns true on success");
    runner.expectEq(static_cast<uint32_t>(0x78563412), val, "readPodChecked: correct value (little-endian)");
  }

  // Test 8: readPodChecked incomplete read
  {
    FsFile file;
    file.setBuffer("\x12\x34");  // Only 2 bytes, need 4

    uint32_t val = 0xFFFFFFFF;
    bool success = serialization::readPodChecked(file, val);
    runner.expectFalse(success, "readPodChecked: returns false on incomplete read");
  }

  // Test 9: readPodChecked empty file
  {
    FsFile file;
    file.setBuffer("");

    uint32_t val = 0xFFFFFFFF;
    bool success = serialization::readPodChecked(file, val);
    runner.expectFalse(success, "readPodChecked: returns false on empty file");
  }

  // ============================================
  // writeString() / readString() tests
  // ============================================

  // Test 10: Empty string
  {
    std::stringstream ss;
    std::string writeStr = "";
    serialization::writeString(ss, writeStr);

    std::string readStr = "garbage";
    bool success = serialization::readString(ss, readStr);
    runner.expectTrue(success, "writeString/readString: empty string success");
    runner.expectEqual("", readStr, "writeString/readString: empty string value");
  }

  // Test 11: ASCII string
  {
    std::stringstream ss;
    std::string writeStr = "Hello, World!";
    serialization::writeString(ss, writeStr);

    std::string readStr;
    bool success = serialization::readString(ss, readStr);
    runner.expectTrue(success, "writeString/readString: ASCII string success");
    runner.expectEqual(writeStr, readStr, "writeString/readString: ASCII string value");
  }

  // Test 12: UTF-8 string
  {
    std::stringstream ss;
    std::string writeStr = "Hello 中文 emoji 😀!";
    serialization::writeString(ss, writeStr);

    std::string readStr;
    bool success = serialization::readString(ss, readStr);
    runner.expectTrue(success, "writeString/readString: UTF-8 string success");
    runner.expectEqual(writeStr, readStr, "writeString/readString: UTF-8 string value");
  }

  // Test 13: String with null bytes
  {
    std::stringstream ss;
    std::string writeStr = "Hello\x00World";
    writeStr[5] = '\x00';  // Ensure null byte is in middle
    serialization::writeString(ss, writeStr);

    std::string readStr;
    bool success = serialization::readString(ss, readStr);
    runner.expectTrue(success, "writeString/readString: string with null success");
    runner.expectEq(writeStr.size(), readStr.size(), "writeString/readString: string with null size");
  }

  // Test 14: String at exactly 65536 bytes (boundary)
  {
    std::stringstream ss;
    std::string writeStr(65536, 'X');
    serialization::writeString(ss, writeStr);

    std::string readStr;
    bool success = serialization::readString(ss, readStr);
    runner.expectTrue(success, "writeString/readString: 65536 byte string success");
    runner.expectEq(static_cast<size_t>(65536), readStr.size(), "writeString/readString: 65536 byte string size");
  }

  // Test 15: String exceeding 65536 bytes (should fail on read)
  {
    // Manually construct a stream with length = 65537
    std::stringstream ss;
    uint32_t fakeLen = 65537;
    ss.write(reinterpret_cast<const char*>(&fakeLen), sizeof(fakeLen));
    // Don't write actual data - the read should fail at length check

    std::string readStr;
    bool success = serialization::readString(ss, readStr);
    runner.expectFalse(success, "readString: rejects length > 65536");
    runner.expectTrue(readStr.empty(), "readString: clears output on length rejection");
  }

  // Test 16: Corrupted length field (huge value)
  {
    std::stringstream ss;
    uint32_t corruptLen = 0xFFFFFFFF;
    ss.write(reinterpret_cast<const char*>(&corruptLen), sizeof(corruptLen));

    std::string readStr;
    bool success = serialization::readString(ss, readStr);
    runner.expectFalse(success, "readString: rejects corrupted length");
  }

  // Test 17: Partial read (not enough data)
  {
    std::stringstream ss;
    uint32_t len = 100;
    ss.write(reinterpret_cast<const char*>(&len), sizeof(len));
    ss.write("short", 5);  // Only 5 bytes, claimed 100

    std::string readStr;
    bool success = serialization::readString(ss, readStr);
    runner.expectFalse(success, "readString: fails on partial data");
  }

  // ============================================
  // writeString() / readString() with FsFile
  // ============================================

  // Test 18: FsFile string roundtrip
  {
    FsFile file;
    file.setBuffer("");

    std::string writeStr = "Test FsFile string";
    serialization::writeString(file, writeStr);

    file.seek(0);
    std::string readStr;
    bool success = serialization::readString(file, readStr);
    runner.expectTrue(success, "writeString/readString FsFile: success");
    runner.expectEqual(writeStr, readStr, "writeString/readString FsFile: correct value");
  }

  // Test 19: FsFile with corrupt length
  {
    FsFile file;
    // Set up buffer with length > 65536
    std::string data;
    uint32_t badLen = 100000;
    data.append(reinterpret_cast<const char*>(&badLen), sizeof(badLen));
    file.setBuffer(data);

    std::string readStr;
    bool success = serialization::readString(file, readStr);
    runner.expectFalse(success, "readString FsFile: rejects length > 65536");
  }

  // ============================================
  // readPodValidated() tests
  // ============================================

  // Test 20: readPodValidated within range
  {
    FsFile file;
    uint16_t val = 100;
    std::string data(reinterpret_cast<const char*>(&val), sizeof(val));
    file.setBuffer(data);

    uint16_t result = 0;
    serialization::readPodValidated(file, result, static_cast<uint16_t>(200));
    runner.expectEq(static_cast<uint16_t>(100), result, "readPodValidated: accepts value within range");
  }

  // Test 21: readPodValidated exceeds max
  {
    FsFile file;
    uint16_t val = 250;
    std::string data(reinterpret_cast<const char*>(&val), sizeof(val));
    file.setBuffer(data);

    uint16_t result = 42;  // Initial value should be preserved
    serialization::readPodValidated(file, result, static_cast<uint16_t>(200));
    runner.expectEq(static_cast<uint16_t>(42), result, "readPodValidated: rejects value exceeding max, keeps original");
  }

  // Test 22: readPodValidated at boundary (value == maxValue)
  {
    FsFile file;
    uint16_t val = 200;
    std::string data(reinterpret_cast<const char*>(&val), sizeof(val));
    file.setBuffer(data);

    uint16_t result = 42;
    serialization::readPodValidated(file, result, static_cast<uint16_t>(200));
    // value < maxValue check means 200 < 200 is false, so should keep original
    runner.expectEq(static_cast<uint16_t>(42), result, "readPodValidated: boundary value (equal) keeps original");
  }

  // Test 23: readPodValidated just below boundary
  {
    FsFile file;
    uint16_t val = 199;
    std::string data(reinterpret_cast<const char*>(&val), sizeof(val));
    file.setBuffer(data);

    uint16_t result = 42;
    serialization::readPodValidated(file, result, static_cast<uint16_t>(200));
    runner.expectEq(static_cast<uint16_t>(199), result, "readPodValidated: just below boundary accepted");
  }

  // ============================================
  // Multiple values in sequence
  // ============================================

  // Test 24: Multiple PODs in sequence
  {
    std::stringstream ss;
    uint8_t a = 1;
    uint16_t b = 2;
    uint32_t c = 3;

    serialization::writePod(ss, a);
    serialization::writePod(ss, b);
    serialization::writePod(ss, c);

    uint8_t ra = 0;
    uint16_t rb = 0;
    uint32_t rc = 0;

    serialization::readPod(ss, ra);
    serialization::readPod(ss, rb);
    serialization::readPod(ss, rc);

    runner.expectEq(a, ra, "Sequential PODs: uint8_t");
    runner.expectEq(b, rb, "Sequential PODs: uint16_t");
    runner.expectEq(c, rc, "Sequential PODs: uint32_t");
  }

  // Test 25: Multiple strings in sequence
  {
    std::stringstream ss;
    serialization::writeString(ss, "First");
    serialization::writeString(ss, "Second");
    serialization::writeString(ss, "Third");

    std::string s1, s2, s3;
    runner.expectTrue(serialization::readString(ss, s1), "Sequential strings: read 1 success");
    runner.expectTrue(serialization::readString(ss, s2), "Sequential strings: read 2 success");
    runner.expectTrue(serialization::readString(ss, s3), "Sequential strings: read 3 success");

    runner.expectEqual("First", s1, "Sequential strings: value 1");
    runner.expectEqual("Second", s2, "Sequential strings: value 2");
    runner.expectEqual("Third", s3, "Sequential strings: value 3");
  }

  return runner.allPassed() ? 0 : 1;
}
