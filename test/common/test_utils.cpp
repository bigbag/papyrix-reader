#include "test_utils.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace TestUtils {

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return {};
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string normalizeLineEndings(const std::string& s) {
  std::string result;
  result.reserve(s.size());
  for (char c : s) {
    if (c != '\r') result += c;
  }
  return result;
}

ComparisonResult compareStrings(const std::string& expected, const std::string& actual) {
  ComparisonResult result;
  result.expectedLength = expected.size();
  result.actualLength = actual.size();

  size_t minSize = std::min(expected.size(), actual.size());
  size_t i = 0;
  while (i < minSize && expected[i] == actual[i]) ++i;

  result.firstDiffIndex = i;

  if (i < minSize) {
    std::ostringstream ess, ass;
    ess << "0x" << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)(unsigned char)expected[i];
    ass << "0x" << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)(unsigned char)actual[i];
    result.expectedByte = ess.str();
    result.actualByte = ass.str();
    result.success = false;
  } else if (expected.size() != actual.size()) {
    result.success = false;
  } else {
    result.success = true;
  }

  return result;
}

void reportComparison(const ComparisonResult& result, const std::string& testName) {
  if (result.success) {
    std::cout << "  PASS: " << testName << "\n";
  } else {
    std::cerr << "  FAIL: " << testName << "\n";
    std::cerr << "    First difference at index " << result.firstDiffIndex << "\n";
    if (!result.expectedByte.empty()) {
      std::cerr << "    Expected byte: " << result.expectedByte << ", Actual byte: " << result.actualByte << "\n";
    } else {
      std::cerr << "    Length mismatch: expected=" << result.expectedLength << ", actual=" << result.actualLength
                << "\n";
    }
  }
}

TestRunner::TestRunner(const std::string& suiteName) : suiteName_(suiteName), passCount_(0), failCount_(0) {
  std::cout << "\n========================================\n";
  std::cout << "Test Suite: " << suiteName_ << "\n";
  std::cout << "========================================\n";
}

TestRunner::~TestRunner() { printSummary(); }

void TestRunner::runTest(const std::string& testName, bool passed, const std::string& failureMessage) {
  if (passed) {
    std::cout << "  \xE2\x9C\x93 PASS: " << testName << "\n";
    passCount_++;
  } else {
    std::cerr << "  \xE2\x9C\x97 FAIL: " << testName << "\n";
    if (!failureMessage.empty()) {
      std::cerr << "    " << failureMessage << "\n";
    }
    failCount_++;
  }
}

bool TestRunner::expectEqual(const std::string& expected, const std::string& actual, const std::string& testName,
                             bool verbose) {
  ComparisonResult result = compareStrings(expected, actual);
  if (result.success) {
    std::cout << "  \xE2\x9C\x93 PASS: " << testName << "\n";
    passCount_++;
    return true;
  } else {
    std::cerr << "  \xE2\x9C\x97 FAIL: " << testName << "\n";
    if (verbose) {
      std::cerr << "    First difference at index " << result.firstDiffIndex << "\n";
      if (!result.expectedByte.empty()) {
        std::cerr << "    Expected byte: " << result.expectedByte << ", Actual byte: " << result.actualByte << "\n";
      } else {
        std::cerr << "    Length mismatch: expected=" << result.expectedLength << ", actual=" << result.actualLength
                  << "\n";
      }
    }
    failCount_++;
    return false;
  }
}

bool TestRunner::expectTrue(bool condition, const std::string& testName, const std::string& message, bool silent) {
  if (condition) {
    if (!silent) {
      std::cout << "  \xE2\x9C\x93 PASS: " << testName << "\n";
    }
    passCount_++;
    return true;
  } else {
    std::cerr << "  \xE2\x9C\x97 FAIL: " << testName << "\n";
    if (!message.empty()) {
      std::cerr << "    " << message << "\n";
    }
    failCount_++;
    return false;
  }
}

void TestRunner::printSummary() const {
  std::cout << "\n========================================\n";
  std::cout << "Test Suite: " << suiteName_ << " - Summary\n";
  std::cout << "========================================\n";
  std::cout << "Total tests: " << (passCount_ + failCount_) << "\n";
  std::cout << "  Passed: " << passCount_ << "\n";
  std::cout << "  Failed: " << failCount_ << "\n";
  if (allPassed()) {
    std::cout << "\n\xE2\x9C\x93 ALL TESTS PASSED\n";
  } else {
    std::cerr << "\n\xE2\x9C\x97 SOME TESTS FAILED\n";
  }
  std::cout << "========================================\n\n";
}

}  // namespace TestUtils
