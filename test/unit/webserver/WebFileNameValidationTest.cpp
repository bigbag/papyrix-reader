#include "test_utils.h"

#include <Utf8Nfc.h>

#include <cstdint>
#include <string>

#include "network/WebFileNameValidation.h"

namespace {

using papyrix::web::FileNameError;

uint8_t code(FileNameError error) { return static_cast<uint8_t>(error); }

void expectName(TestUtils::TestRunner& runner, const std::string& name, FileNameError expected,
                const char* testName) {
  runner.expectEq(code(expected), code(papyrix::web::validateFileName(name.data(), name.size())), testName);
}

void expectPath(TestUtils::TestRunner& runner, const std::string& path, bool expected, const char* testName) {
  runner.expectEq(expected, papyrix::web::isSafeWebPath(path.data(), path.size()), testName);
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("WebFileNameValidation");

  expectName(runner, "Books", FileNameError::None, "ASCII accepted");
  expectName(runner, "Book 1.epub", FileNameError::None, "spaces and period accepted");
  expectName(runner, u8"Книги", FileNameError::None, "Cyrillic accepted");
  expectName(runner, u8"Sách", FileNameError::None, "Vietnamese accepted");
  expectName(runner, u8"Tiếng Việt", FileNameError::None, "Vietnamese phrase accepted");
  expectName(runner, u8"Δοκιμή", FileNameError::None, "Greek accepted");
  expectName(runner, u8"หนังสือ", FileNameError::None, "Thai accepted");
  expectName(runner, u8"كتاب", FileNameError::None, "Arabic accepted");

  expectName(runner, "", FileNameError::Empty, "empty rejected");
  expectName(runner, ".", FileNameError::HiddenName, "dot rejected");
  expectName(runner, "..", FileNameError::HiddenName, "dot dot rejected");
  expectName(runner, ".hidden", FileNameError::HiddenName, "dot prefix rejected");

  const char* reserved = "\"*/:<>?\\|";
  for (const char* p = reserved; *p; ++p) {
    std::string name = "a";
    name.push_back(*p);
    name += "b";
    expectName(runner, name, FileNameError::InvalidCharacter,
               (std::string("reserved character ") + *p + " rejected").c_str());
  }

  for (uint8_t value = 0; value < 0x20; ++value) {
    std::string name("a", 1);
    name.push_back(static_cast<char>(value));
    name.push_back('b');
    expectName(runner, name, FileNameError::InvalidCharacter,
               (std::string("control byte ") + std::to_string(value) + " rejected").c_str());
  }

  expectName(runner, std::string("\xC0\xAF", 2), FileNameError::InvalidUtf8, "overlong UTF-8 rejected");
  expectName(runner, std::string("\xE2\x82", 2), FileNameError::InvalidUtf8, "truncated UTF-8 rejected");
  expectName(runner, std::string("\xED\xA0\x80", 3), FileNameError::InvalidUtf8, "UTF-8 surrogate rejected");
  expectName(runner, std::string("\xF4\x90\x80\x80", 4), FileNameError::InvalidUtf8,
             "out of range UTF-8 rejected");

  expectName(runner, u8"中文", FileNameError::UnsupportedCjk, "Chinese rejected");
  expectName(runner, u8"かな", FileNameError::UnsupportedCjk, "Hiragana rejected");
  expectName(runner, u8"カナ", FileNameError::UnsupportedCjk, "Katakana rejected");
  expectName(runner, u8"한글", FileNameError::UnsupportedCjk, "Hangul rejected");
  expectName(runner, std::string("\xF0\xA0\x80\x80", 4), FileNameError::UnsupportedCjk,
             "CJK extension rejected");

  expectName(runner, std::string(255, 'a'), FileNameError::None, "255-byte name accepted");
  expectName(runner, std::string(256, 'a'), FileNameError::TooLong, "256-byte name rejected");
  expectName(runner, std::string(253, 'a') + u8"é", FileNameError::None,
             "multibyte name ending at 255 bytes accepted");
  expectName(runner, std::string(254, 'a') + u8"é", FileNameError::TooLong,
             "multibyte name crossing 255 bytes rejected");

  std::string decomposed = std::string("Sa") + "\xCC\x81" + "ch";
  runner.expectTrue(papyrix::web::isValidUtf8(decomposed.data(), decomposed.size()),
                    "decomposed Vietnamese is valid UTF-8");
  const size_t normalizedLength = utf8NormalizeNfc(decomposed.data(), decomposed.size());
  decomposed.resize(normalizedLength);
  runner.expectEqual(u8"Sách", decomposed, "Vietnamese normalized to NFC");
  expectName(runner, decomposed, FileNameError::None, "normalized Vietnamese accepted");

  expectPath(runner, "/Books/Novel.epub", true, "normal absolute path accepted");
  expectPath(runner, u8"/Книги/Sách.epub", true, "Unicode path accepted");
  expectPath(runner, "/", true, "root path accepted");
  expectPath(runner, "Books/Novel.epub", false, "relative path rejected");
  expectPath(runner, "/.papyrix/wifi.bin", false, "internal path rejected");
  expectPath(runner, "/Books/.hidden/book.epub", false, "hidden component rejected");
  expectPath(runner, "/Books/../wifi.bin", false, "parent traversal rejected");
  expectPath(runner, "/Books/./Novel.epub", false, "current-directory component rejected");
  expectPath(runner, "/Books\\Novel.epub", false, "backslash rejected");
  expectPath(runner, std::string("/Books/") + char(1) + "Novel.epub", false, "control byte rejected");
  expectPath(runner, "/" + std::string(1023, 'a'), false, "path over byte limit rejected");

  runner.expectTrue(papyrix::web::canAppendPathComponent(1018, false, 4),
                    "path exactly 1023 bytes accepted");
  runner.expectFalse(papyrix::web::canAppendPathComponent(1018, false, 5),
                     "path over 1023 bytes rejected");
  runner.expectTrue(papyrix::web::canAppendPathComponent(1, true, 255),
                    "root path does not add duplicate slash");

  return runner.allPassed() ? 0 : 1;
}
