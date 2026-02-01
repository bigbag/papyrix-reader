#pragma once

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace TestUtils {

// File I/O helpers
std::string readFile(const std::string& path);
std::string normalizeLineEndings(const std::string& s);

// Text comparison helpers
struct ComparisonResult {
  bool success;
  size_t firstDiffIndex;
  std::string expectedByte;
  std::string actualByte;
  size_t expectedLength;
  size_t actualLength;
};

ComparisonResult compareStrings(const std::string& expected, const std::string& actual);
void reportComparison(const ComparisonResult& result, const std::string& testName);

// Test result tracking
class TestRunner {
 public:
  TestRunner(const std::string& suiteName);
  ~TestRunner();

  void runTest(const std::string& testName, bool passed, const std::string& failureMessage = "");
  bool expectEqual(const std::string& expected, const std::string& actual, const std::string& testName,
                   bool verbose = false);
  bool expectTrue(bool condition, const std::string& testName, const std::string& message = "", bool silent = false);

  // Numeric comparison helpers
  template <typename T>
  bool expectEq(T expected, T actual, const std::string& testName) {
    if (expected == actual) {
      std::cout << "  \xE2\x9C\x93 PASS: " << testName << "\n";
      passCount_++;
      return true;
    } else {
      std::cerr << "  \xE2\x9C\x97 FAIL: " << testName << "\n";
      std::cerr << "    Expected: " << expected << ", Actual: " << actual << "\n";
      failCount_++;
      return false;
    }
  }

  template <typename T>
  bool expectNe(T expected, T actual, const std::string& testName) {
    if (expected != actual) {
      std::cout << "  \xE2\x9C\x93 PASS: " << testName << "\n";
      passCount_++;
      return true;
    } else {
      std::cerr << "  \xE2\x9C\x97 FAIL: " << testName << "\n";
      std::cerr << "    Expected not equal to: " << expected << "\n";
      failCount_++;
      return false;
    }
  }

  bool expectFloatEq(float expected, float actual, const std::string& testName, float epsilon = 0.001f) {
    if (std::fabs(expected - actual) < epsilon) {
      std::cout << "  \xE2\x9C\x93 PASS: " << testName << "\n";
      passCount_++;
      return true;
    } else {
      std::cerr << "  \xE2\x9C\x97 FAIL: " << testName << "\n";
      std::cerr << "    Expected: " << expected << ", Actual: " << actual << "\n";
      failCount_++;
      return false;
    }
  }

  bool expectFalse(bool condition, const std::string& testName, const std::string& message = "") {
    return expectTrue(!condition, testName, message);
  }

  int getPassCount() const { return passCount_; }
  int getFailCount() const { return failCount_; }
  bool allPassed() const { return failCount_ == 0; }

  void printSummary() const;

 private:
  std::string suiteName_;
  int passCount_;
  int failCount_;
};

}  // namespace TestUtils
