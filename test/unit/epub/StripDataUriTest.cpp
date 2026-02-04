#include "test_utils.h"

#include <cstring>
#include <string>

#include "parsers/DataUriStripper.h"

int main() {
  TestUtils::TestRunner runner("StripDataUris Tests");

  // ============================================
  // Test 1: No data URI - should pass through unchanged
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img src=\"image.jpg\" alt=\"test\">";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("<img src=\"image.jpg\" alt=\"test\">"), std::string(buf),
                       "No data URI: pass through unchanged");
  }

  // ============================================
  // Test 2: Simple data URI with double quotes
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img src=\"data:image/jpeg;base64,ABC123\" alt=\"test\">";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("<img src=\"#\" alt=\"test\">"), std::string(buf),
                       "Data URI with double quotes: replaced with #");
  }

  // ============================================
  // Test 3: Data URI with single quotes
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img src='data:image/jpeg;base64,ABC123' alt='test'>";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("<img src='#' alt='test'>"), std::string(buf),
                       "Data URI with single quotes: replaced with #");
  }

  // ============================================
  // Test 4: Case insensitive - uppercase SRC
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img SRC=\"data:image/jpeg;base64,ABC123\">";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("<img SRC=\"#\">"), std::string(buf),
                       "Uppercase SRC: replaced with #");
  }

  // ============================================
  // Test 5: Mixed case Src
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img Src=\"data:image/jpeg;base64,ABC123\">";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("<img Src=\"#\">"), std::string(buf),
                       "Mixed case Src: replaced with #");
  }

  // ============================================
  // Test 6: Empty buffer
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "";
    size_t len = 0;
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    runner.expectEq(static_cast<size_t>(0), newLen, "Empty buffer: returns 0");
  }

  // ============================================
  // Test 7: Data URI at the very beginning
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "src=\"data:image/png;base64,iVBORw0KGgo\"";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("src=\"#\""), std::string(buf),
                       "Data URI at beginning: replaced with #");
  }

  // ============================================
  // Test 8: Multiple data URIs in same buffer
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img src=\"data:image/jpeg;base64,ABC\"> and <img src=\"data:image/png;base64,XYZ\">";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("<img src=\"#\"> and <img src=\"#\">"), std::string(buf),
                       "Multiple data URIs: both replaced with #");
  }

  // ============================================
  // Test 9: Regular URL that starts with 'data' but isn't a data URI
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img src=\"database/image.jpg\">";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("<img src=\"database/image.jpg\">"), std::string(buf),
                       "URL starting with 'database': not modified");
  }

  // ============================================
  // Test 10: Long data URI (simulating real base64 image)
  // ============================================
  {
    DataUriStripper stripper;
    // Simulate a very long data URI
    std::string longData = "<img src=\"data:image/jpeg;base64,";
    for (int i = 0; i < 1000; i++) {
      longData += "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    }
    longData += "\" alt=\"test\">";

    size_t allocatedSize = longData.length() + 1;
    char* buf = new char[allocatedSize];
    memcpy(buf, longData.c_str(), longData.length());
    size_t len = longData.length();

    size_t newLen = stripper.strip(buf, len, allocatedSize);
    buf[newLen] = '\0';

    std::string expected = "<img src=\"#\" alt=\"test\">";
    runner.expectEqual(expected, std::string(buf),
                       "Long data URI: correctly shortened to #");

    // Verify the new length is much smaller
    runner.expectTrue(newLen < len / 10, "Long data URI: length significantly reduced");

    delete[] buf;
  }

  // ============================================
  // Test 11: Data URI at end of buffer without closing quote
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img src=\"data:image/jpeg;base64,ABC";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    // Should strip from src="data: to end since no closing quote
    runner.expectEqual(std::string("<img src=\"#\""), std::string(buf),
                       "Data URI without closing quote: replaced with #");
  }

  // ============================================
  // Test 12: Non-data URI attribute that looks similar
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<a href=\"data-something\">link</a>";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("<a href=\"data-something\">link</a>"), std::string(buf),
                       "Non-src data attribute: not modified");
  }

  // ============================================
  // Test 13: Data URI split across buffer boundaries
  // ============================================
  {
    DataUriStripper stripper;
    // First chunk ends with 'src="dat'
    char buf1[] = "<img src=\"dat";
    size_t len1 = strlen(buf1);
    size_t newLen1 = stripper.strip(buf1, len1, sizeof(buf1));
    buf1[newLen1] = '\0';
    // Should save partial match and output just '<img '
    runner.expectEqual(std::string("<img "), std::string(buf1),
                       "Partial data URI (chunk 1): partial match saved");

    // Second chunk continues with 'a:image/png;base64,ABC" alt="test">'
    // Buffer needs extra capacity for prepended partial match (up to 9 bytes)
    char buf2[128] = "a:image/png;base64,ABC\" alt=\"test\">";
    size_t len2 = strlen(buf2);
    size_t newLen2 = stripper.strip(buf2, len2, sizeof(buf2));
    buf2[newLen2] = '\0';
    // Should prepend partial match and process complete data URI
    runner.expectEqual(std::string("src=\"#\" alt=\"test\">"), std::string(buf2),
                       "Partial data URI (chunk 2): complete pattern processed");
  }

  // ============================================
  // Test 14: Case insensitive data URI check in cacheImage
  // ============================================
  {
    // Test that strncasecmp behavior matches what we expect
    const char* uri1 = "data:image/png;base64,ABC";
    const char* uri2 = "DATA:image/png;base64,ABC";
    const char* uri3 = "Data:image/png;base64,ABC";
    const char* uri4 = "database/image.jpg";

    runner.expectTrue(strncasecmp(uri1, "data:", 5) == 0, "Lowercase data: matches");
    runner.expectTrue(strncasecmp(uri2, "data:", 5) == 0, "Uppercase DATA: matches");
    runner.expectTrue(strncasecmp(uri3, "data:", 5) == 0, "Mixed case Data: matches");
    runner.expectTrue(strncasecmp(uri4, "data:", 5) != 0, "database: does not match");
  }

  // ============================================
  // Test 15: Case insensitive DATA: in stripper
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img src=\"DATA:image/jpeg;base64,ABC123\">";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("<img src=\"#\">"), std::string(buf),
                       "Uppercase DATA: in stripper: replaced with #");
  }

  // ============================================
  // Test 16: Mixed case DaTa: in stripper
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img src=\"DaTa:image/jpeg;base64,ABC123\">";
    size_t len = strlen(buf);
    size_t newLen = stripper.strip(buf, len, sizeof(buf));
    buf[newLen] = '\0';
    runner.expectEqual(std::string("<img src=\"#\">"), std::string(buf),
                       "Mixed case DaTa: in stripper: replaced with #");
  }

  // ============================================
  // Test 17: Data URI content spanning multiple buffers
  // ============================================
  {
    DataUriStripper stripper;
    // First chunk: complete src="data: pattern, but content continues
    char buf1[] = "<img src=\"data:image/jpeg;base64,ABCDEFGHIJ";
    size_t len1 = strlen(buf1);
    size_t newLen1 = stripper.strip(buf1, len1, sizeof(buf1));
    buf1[newLen1] = '\0';
    // Should output src="#" and set state to skip remaining content
    runner.expectEqual(std::string("<img src=\"#\""), std::string(buf1),
                       "Data URI spanning buffers (chunk 1): replaced with #");

    // Second chunk: more base64 content
    char buf2[] = "KLMNOPQRSTUVWXYZ0123456789";
    size_t len2 = strlen(buf2);
    size_t newLen2 = stripper.strip(buf2, len2, sizeof(buf2));
    // Should skip all content (still looking for closing quote)
    runner.expectEq(static_cast<size_t>(0), newLen2,
                    "Data URI spanning buffers (chunk 2): content skipped");

    // Third chunk: closing quote and more HTML
    char buf3[] = "\" alt=\"test\"><p>Hello</p>";
    size_t len3 = strlen(buf3);
    size_t newLen3 = stripper.strip(buf3, len3, sizeof(buf3));
    buf3[newLen3] = '\0';
    // Should skip up to closing quote, then output rest
    runner.expectEqual(std::string(" alt=\"test\"><p>Hello</p>"), std::string(buf3),
                       "Data URI spanning buffers (chunk 3): rest of HTML preserved");
  }

  // ============================================
  // Test 18: Reset clears spanning state
  // ============================================
  {
    DataUriStripper stripper;
    // Start a data URI that spans buffers
    char buf1[] = "<img src=\"data:image/jpeg;base64,ABC";
    size_t len1 = strlen(buf1);
    stripper.strip(buf1, len1, sizeof(buf1));

    // Reset the stripper (simulates starting a new file)
    stripper.reset();

    // New content should be processed normally, not skipped
    char buf2[] = "<p>Normal content</p>";
    size_t len2 = strlen(buf2);
    size_t newLen2 = stripper.strip(buf2, len2, sizeof(buf2));
    buf2[newLen2] = '\0';
    runner.expectEqual(std::string("<p>Normal content</p>"), std::string(buf2),
                       "Reset clears spanning state: normal content preserved");
  }

  // ============================================
  // Test 19: Invalid capacity returns 0
  // ============================================
  {
    DataUriStripper stripper;
    char buf[] = "<img src=\"image.jpg\">";
    size_t len = strlen(buf);
    // Pass capacity smaller than length - should return 0
    size_t newLen = stripper.strip(buf, len, len - 1);
    runner.expectEq(static_cast<size_t>(0), newLen,
                    "Invalid capacity (< len): returns 0");
  }

  // ============================================
  // Test 20: Partial match skipped when buffer capacity too small
  // ============================================
  {
    DataUriStripper stripper;
    // First chunk ends with partial match (needs at least 10 chars written for partial detection)
    char buf1[64] = "0123456789<img sr";  // 17 chars, writePos will be 15 after partial saved
    size_t len1 = strlen(buf1);
    size_t newLen1 = stripper.strip(buf1, len1, sizeof(buf1));
    buf1[newLen1] = '\0';
    // Should save partial match 'sr' and output up to '<img '
    runner.expectEqual(std::string("0123456789<img "), std::string(buf1),
                       "Partial match setup: saved partial");

    // Second chunk with capacity too small for prepend (partial is 2 bytes, len is 6, need 8 but only have 7)
    char buf2[7] = "c=\"x\"";  // 5 chars + null, partial 'sr' = 2 bytes, total 7 but capacity is only 7
    size_t len2 = strlen(buf2);
    // Capacity is 7, len is 5, partial is 2. 5 + 2 = 7, but we check partialLen_ <= bufCapacity && len <= bufCapacity - partialLen_
    // 2 <= 7 is true, 5 <= 7-2=5 is true. So it should fit!
    // Let's make capacity truly too small
    size_t newLen2 = stripper.strip(buf2, len2, 6);  // capacity 6 < len(5) + partial(2)
    buf2[newLen2] = '\0';
    // Since capacity (6) < len (5) + partial (2) = 7, partial is NOT prepended
    // Output is just the buffer content without the partial
    runner.expectEqual(std::string("c=\"x\""), std::string(buf2),
                       "Partial not prepended when capacity too small");
  }

  return runner.allPassed() ? 0 : 1;
}
