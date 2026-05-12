#include "test_utils.h"

#include <cstring>
#include <string>

#include "HardwareSerial.h"
#include "SdFat.h"
#include "Serialization.h"

namespace {

constexpr uint32_t SETTINGS_MAGIC = 0x53585050;

// Old sizes (version <= 10)
constexpr size_t OLD_LAST_BOOK_PATH_SIZE = 256;
constexpr size_t OLD_FILE_LIST_DIR_SIZE = 256;
constexpr size_t OLD_FILE_LIST_SELECTED_NAME_SIZE = 128;

// New sizes (version >= 11)
constexpr size_t NEW_LAST_BOOK_PATH_SIZE = 512;
constexpr size_t NEW_FILE_LIST_DIR_SIZE = 512;
constexpr size_t NEW_FILE_LIST_SELECTED_NAME_SIZE = 256;

struct SettingsV10 {
  uint8_t sleepScreen = 0;
  uint8_t textLayout = 1;
  uint8_t shortPwrBtn = 0;
  uint8_t statusBar = 1;
  uint8_t orientation = 0;
  uint8_t fontSize = 2;
  uint8_t pagesPerRefresh = 5;
  uint8_t sideButtonLayout = 0;
  uint8_t autoSleepMinutes = 1;
  uint8_t paragraphAlignment = 0;
  uint8_t hyphenation = 1;
  uint8_t textAntiAliasing = 0;
  uint8_t showImages = 1;
  uint8_t startupBehavior = 0;
  uint8_t _reserved = 0;
  uint8_t lineSpacing = 1;
  char themeName[32] = "light";
  char lastBookPath[OLD_LAST_BOOK_PATH_SIZE] = "";
  uint8_t pendingTransition = 0;
  uint8_t transitionReturnTo = 0;
  uint8_t sunlightFadingFix = 0;
  char fileListDir[OLD_FILE_LIST_DIR_SIZE] = "/";
  char fileListSelectedName[OLD_FILE_LIST_SELECTED_NAME_SIZE] = "";
  uint16_t fileListSelectedIndex = 0;
  uint8_t frontButtonLayout = 0;
  uint8_t fullBookProcess = 0;
};

void writeV10Settings(FsFile& file, const SettingsV10& s) {
  uint8_t version = 10;
  uint8_t count = 26;
  serialization::writePod(file, SETTINGS_MAGIC);
  serialization::writePod(file, version);
  serialization::writePod(file, count);
  serialization::writePod(file, s.sleepScreen);
  serialization::writePod(file, s.textLayout);
  serialization::writePod(file, s.shortPwrBtn);
  serialization::writePod(file, s.statusBar);
  serialization::writePod(file, s.orientation);
  serialization::writePod(file, s.fontSize);
  serialization::writePod(file, s.pagesPerRefresh);
  serialization::writePod(file, s.sideButtonLayout);
  serialization::writePod(file, s.autoSleepMinutes);
  serialization::writePod(file, s.paragraphAlignment);
  serialization::writePod(file, s.hyphenation);
  serialization::writePod(file, s.textAntiAliasing);
  serialization::writePod(file, s.showImages);
  serialization::writePod(file, s.startupBehavior);
  serialization::writePod(file, s._reserved);
  serialization::writePod(file, s.lineSpacing);
  file.write(reinterpret_cast<const uint8_t*>(s.themeName), sizeof(s.themeName));
  file.write(reinterpret_cast<const uint8_t*>(s.lastBookPath), OLD_LAST_BOOK_PATH_SIZE);
  serialization::writePod(file, s.pendingTransition);
  serialization::writePod(file, s.transitionReturnTo);
  serialization::writePod(file, s.sunlightFadingFix);
  file.write(reinterpret_cast<const uint8_t*>(s.fileListDir), OLD_FILE_LIST_DIR_SIZE);
  file.write(reinterpret_cast<const uint8_t*>(s.fileListSelectedName), OLD_FILE_LIST_SELECTED_NAME_SIZE);
  serialization::writePod(file, s.fileListSelectedIndex);
  serialization::writePod(file, s.frontButtonLayout);
  serialization::writePod(file, s.fullBookProcess);
}

struct LoadedPaths {
  char lastBookPath[NEW_LAST_BOOK_PATH_SIZE] = "";
  char fileListDir[NEW_FILE_LIST_DIR_SIZE] = "/";
  char fileListSelectedName[NEW_FILE_LIST_SELECTED_NAME_SIZE] = "";
  uint16_t fileListSelectedIndex = 0;
  uint8_t pendingTransition = 0;
  uint8_t transitionReturnTo = 0;
  uint8_t sunlightFadingFix = 0;
  uint8_t frontButtonLayout = 0;
  uint8_t fullBookProcess = 0;
};

// Migration logic matching PapyrixSettings.cpp
bool loadPathFields(FsFile& file, uint8_t version, uint8_t fileSettingsCount, uint8_t& settingsRead,
                    LoadedPaths& out) {
  if (version <= 10) {
    file.read(reinterpret_cast<uint8_t*>(out.lastBookPath), 256);
    memset(out.lastBookPath + 256, 0, sizeof(out.lastBookPath) - 256);
  } else {
    file.read(reinterpret_cast<uint8_t*>(out.lastBookPath), sizeof(out.lastBookPath));
  }
  out.lastBookPath[sizeof(out.lastBookPath) - 1] = '\0';
  if (++settingsRead >= fileSettingsCount) return true;

  serialization::readPod(file, out.pendingTransition);
  if (++settingsRead >= fileSettingsCount) return true;
  serialization::readPod(file, out.transitionReturnTo);
  if (++settingsRead >= fileSettingsCount) return true;
  serialization::readPod(file, out.sunlightFadingFix);
  if (++settingsRead >= fileSettingsCount) return true;

  if (version <= 10) {
    file.read(reinterpret_cast<uint8_t*>(out.fileListDir), 256);
    memset(out.fileListDir + 256, 0, sizeof(out.fileListDir) - 256);
  } else {
    file.read(reinterpret_cast<uint8_t*>(out.fileListDir), sizeof(out.fileListDir));
  }
  out.fileListDir[sizeof(out.fileListDir) - 1] = '\0';
  if (++settingsRead >= fileSettingsCount) return true;

  if (version <= 10) {
    file.read(reinterpret_cast<uint8_t*>(out.fileListSelectedName), 128);
    memset(out.fileListSelectedName + 128, 0, sizeof(out.fileListSelectedName) - 128);
  } else {
    file.read(reinterpret_cast<uint8_t*>(out.fileListSelectedName), sizeof(out.fileListSelectedName));
  }
  out.fileListSelectedName[sizeof(out.fileListSelectedName) - 1] = '\0';
  if (++settingsRead >= fileSettingsCount) return true;

  serialization::readPod(file, out.fileListSelectedIndex);
  if (++settingsRead >= fileSettingsCount) return true;
  serialization::readPod(file, out.frontButtonLayout);
  if (++settingsRead >= fileSettingsCount) return true;
  serialization::readPod(file, out.fullBookProcess);
  if (++settingsRead >= fileSettingsCount) return true;

  return true;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("SettingsPathMigration");

  // === V10 file with short paths migrates correctly ===

  {
    SettingsV10 v10;
    strncpy(v10.lastBookPath, "/books/test.epub", sizeof(v10.lastBookPath) - 1);
    strncpy(v10.fileListDir, "/books", sizeof(v10.fileListDir) - 1);
    strncpy(v10.fileListSelectedName, "test.epub", sizeof(v10.fileListSelectedName) - 1);
    v10.fileListSelectedIndex = 5;
    v10.frontButtonLayout = 1;
    v10.fullBookProcess = 1;

    FsFile file;
    file.setBuffer("");
    writeV10Settings(file, v10);
    file.seek(0);

    // Skip header + first 17 fields (magic + version + count + 14 uint8_t + 1 lineSpacing + themeName)
    uint32_t magic;
    uint8_t version, count;
    serialization::readPod(file, magic);
    serialization::readPod(file, version);
    serialization::readPod(file, count);

    // Skip 16 uint8_t fields
    for (int i = 0; i < 16; i++) {
      uint8_t dummy;
      serialization::readPod(file, dummy);
    }
    // Skip themeName[32]
    char themeDummy[32];
    file.read(reinterpret_cast<uint8_t*>(themeDummy), 32);

    uint8_t settingsRead = 17;  // Already read 17 fields

    LoadedPaths loaded;
    loadPathFields(file, version, count, settingsRead, loaded);

    runner.expectEqual("/books/test.epub", loaded.lastBookPath, "v10 lastBookPath preserved");
    runner.expectEqual("/books", loaded.fileListDir, "v10 fileListDir preserved");
    runner.expectEqual("test.epub", loaded.fileListSelectedName, "v10 fileListSelectedName preserved");
    runner.expectEq(uint16_t(5), loaded.fileListSelectedIndex, "v10 fileListSelectedIndex preserved");
    runner.expectEq(uint8_t(1), loaded.frontButtonLayout, "v10 frontButtonLayout preserved");
    runner.expectEq(uint8_t(1), loaded.fullBookProcess, "v10 fullBookProcess preserved");
  }

  // === V10 file with path at exactly 255 bytes (max for old buffer) ===

  {
    // Build a 255-byte path that fills the old buffer completely
    char longPath[256];
    memset(longPath, 0, sizeof(longPath));
    longPath[0] = '/';
    for (int i = 1; i < 250; i++) longPath[i] = 'a';
    strncpy(longPath + 250, ".epub", 6);  // ".epub" + null

    SettingsV10 v10;
    strncpy(v10.lastBookPath, longPath, sizeof(v10.lastBookPath) - 1);
    v10.lastBookPath[sizeof(v10.lastBookPath) - 1] = '\0';

    FsFile file;
    file.setBuffer("");
    writeV10Settings(file, v10);
    file.seek(0);

    uint32_t magic;
    uint8_t version, count;
    serialization::readPod(file, magic);
    serialization::readPod(file, version);
    serialization::readPod(file, count);
    for (int i = 0; i < 16; i++) {
      uint8_t dummy;
      serialization::readPod(file, dummy);
    }
    char themeDummy[32];
    file.read(reinterpret_cast<uint8_t*>(themeDummy), 32);

    uint8_t settingsRead = 17;
    LoadedPaths loaded;
    loadPathFields(file, version, count, settingsRead, loaded);

    runner.expectEqual(longPath, loaded.lastBookPath, "v10 max-length path preserved");
  }

  // === V10 path fields don't bleed into subsequent fields ===

  {
    SettingsV10 v10;
    strncpy(v10.lastBookPath, "/book.epub", sizeof(v10.lastBookPath) - 1);
    v10.pendingTransition = 2;
    v10.transitionReturnTo = 1;
    v10.sunlightFadingFix = 1;
    strncpy(v10.fileListDir, "/mydir", sizeof(v10.fileListDir) - 1);
    strncpy(v10.fileListSelectedName, "myfile.fb2", sizeof(v10.fileListSelectedName) - 1);
    v10.fileListSelectedIndex = 42;

    FsFile file;
    file.setBuffer("");
    writeV10Settings(file, v10);
    file.seek(0);

    uint32_t magic;
    uint8_t version, count;
    serialization::readPod(file, magic);
    serialization::readPod(file, version);
    serialization::readPod(file, count);
    for (int i = 0; i < 16; i++) {
      uint8_t dummy;
      serialization::readPod(file, dummy);
    }
    char themeDummy[32];
    file.read(reinterpret_cast<uint8_t*>(themeDummy), 32);

    uint8_t settingsRead = 17;
    LoadedPaths loaded;
    loadPathFields(file, version, count, settingsRead, loaded);

    runner.expectEqual("/book.epub", loaded.lastBookPath, "v10 no-bleed lastBookPath");
    runner.expectEq(uint8_t(2), loaded.pendingTransition, "v10 no-bleed pendingTransition");
    runner.expectEq(uint8_t(1), loaded.transitionReturnTo, "v10 no-bleed transitionReturnTo");
    runner.expectEq(uint8_t(1), loaded.sunlightFadingFix, "v10 no-bleed sunlightFadingFix");
    runner.expectEqual("/mydir", loaded.fileListDir, "v10 no-bleed fileListDir");
    runner.expectEqual("myfile.fb2", loaded.fileListSelectedName, "v10 no-bleed fileListSelectedName");
    runner.expectEq(uint16_t(42), loaded.fileListSelectedIndex, "v10 no-bleed fileListSelectedIndex");
  }

  // === V11 roundtrip with paths > 256 bytes ===

  {
    // Build a long path (300 bytes) that only fits in v11 buffers
    char longPath[301];
    memset(longPath, 0, sizeof(longPath));
    longPath[0] = '/';
    for (int i = 1; i < 295; i++) longPath[i] = 'x';
    strncpy(longPath + 295, ".epub", 6);

    char longDir[400];
    memset(longDir, 0, sizeof(longDir));
    longDir[0] = '/';
    for (int i = 1; i < 350; i++) longDir[i] = 'd';

    char longName[200];
    memset(longName, 0, sizeof(longName));
    for (int i = 0; i < 190; i++) longName[i] = 'n';
    strncpy(longName + 190, ".fb2", 5);

    // Write as v11 format
    FsFile file;
    file.setBuffer("");

    uint8_t version = 11;
    uint8_t count = 26;
    serialization::writePod(file, SETTINGS_MAGIC);
    serialization::writePod(file, version);
    serialization::writePod(file, count);
    // 16 uint8_t fields
    for (int i = 0; i < 16; i++) serialization::writePod(file, uint8_t(0));
    // themeName[32]
    char theme[32] = "dark";
    file.write(reinterpret_cast<const uint8_t*>(theme), 32);
    // lastBookPath[512]
    char pathBuf[NEW_LAST_BOOK_PATH_SIZE] = {};
    strncpy(pathBuf, longPath, sizeof(pathBuf) - 1);
    file.write(reinterpret_cast<const uint8_t*>(pathBuf), NEW_LAST_BOOK_PATH_SIZE);
    // pendingTransition, transitionReturnTo, sunlightFadingFix
    serialization::writePod(file, uint8_t(0));
    serialization::writePod(file, uint8_t(0));
    serialization::writePod(file, uint8_t(0));
    // fileListDir[512]
    char dirBuf[NEW_FILE_LIST_DIR_SIZE] = {};
    strncpy(dirBuf, longDir, sizeof(dirBuf) - 1);
    file.write(reinterpret_cast<const uint8_t*>(dirBuf), NEW_FILE_LIST_DIR_SIZE);
    // fileListSelectedName[256]
    char nameBuf[NEW_FILE_LIST_SELECTED_NAME_SIZE] = {};
    strncpy(nameBuf, longName, sizeof(nameBuf) - 1);
    file.write(reinterpret_cast<const uint8_t*>(nameBuf), NEW_FILE_LIST_SELECTED_NAME_SIZE);
    // fileListSelectedIndex
    serialization::writePod(file, uint16_t(99));
    serialization::writePod(file, uint8_t(1));
    serialization::writePod(file, uint8_t(0));

    file.seek(0);

    // Read back
    uint32_t magic;
    serialization::readPod(file, magic);
    serialization::readPod(file, version);
    serialization::readPod(file, count);
    for (int i = 0; i < 16; i++) {
      uint8_t dummy;
      serialization::readPod(file, dummy);
    }
    char themeDummy[32];
    file.read(reinterpret_cast<uint8_t*>(themeDummy), 32);

    uint8_t settingsRead = 17;
    LoadedPaths loaded;
    loadPathFields(file, version, count, settingsRead, loaded);

    runner.expectEqual(longPath, loaded.lastBookPath, "v11 long lastBookPath (300 bytes)");
    runner.expectEqual(longDir, loaded.fileListDir, "v11 long fileListDir (350 bytes)");
    runner.expectEqual(longName, loaded.fileListSelectedName, "v11 long selectedName (194 bytes)");
    runner.expectEq(uint16_t(99), loaded.fileListSelectedIndex, "v11 long path selectedIndex");
  }

  // === Buffer size constants are correct ===

  runner.expectEq(size_t(512), NEW_LAST_BOOK_PATH_SIZE, "lastBookPath buffer is 512");
  runner.expectEq(size_t(512), NEW_FILE_LIST_DIR_SIZE, "fileListDir buffer is 512");
  runner.expectEq(size_t(256), NEW_FILE_LIST_SELECTED_NAME_SIZE, "fileListSelectedName buffer is 256");

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
