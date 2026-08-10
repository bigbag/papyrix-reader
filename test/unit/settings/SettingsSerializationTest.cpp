#include <cstring>
#include <string>

#include "HardwareSerial.h"
#include "PapyrixSettings.h"
#include "Serialization.h"
#include "SettingsSerialization.h"
#include "test_utils.h"

namespace {

FsFile headerFile(uint32_t magic, uint8_t version, uint8_t count) {
  FsFile file;
  file.setBuffer("");
  serialization::writePod(file, magic);
  serialization::writePod(file, version);
  serialization::writePod(file, count);
  file.seek(0);
  return file;
}

void expectStatus(TestUtils::TestRunner& runner, papyrix::SettingsReadStatus expected,
                  papyrix::SettingsReadStatus actual, const std::string& name) {
  runner.expectEq<int>(static_cast<int>(expected), static_cast<int>(actual), name);
}

void writeFirstSixteenFields(FsFile& file, const papyrix::Settings& settings) {
  serialization::writePod(file, settings.sleepScreen);
  serialization::writePod(file, settings.textLayout);
  serialization::writePod(file, settings.shortPwrBtn);
  serialization::writePod(file, settings.statusBar);
  serialization::writePod(file, settings.orientation);
  serialization::writePod(file, settings.fontSize);
  serialization::writePod(file, settings.pagesPerRefresh);
  serialization::writePod(file, settings.sideButtonLayout);
  serialization::writePod(file, settings.autoSleepMinutes);
  serialization::writePod(file, settings.paragraphAlignment);
  serialization::writePod(file, settings.hyphenation);
  serialization::writePod(file, settings.textAntiAliasing);
  serialization::writePod(file, settings.showImages);
  serialization::writePod(file, settings.startupBehavior);
  serialization::writePod(file, settings._reserved);
  serialization::writePod(file, settings.lineSpacing);
}

}  // namespace

int main() {
  using namespace papyrix;
  TestUtils::TestRunner runner("SettingsSerialization");

  for (size_t length = 0; length < sizeof(uint32_t) + 2; length++) {
    FsFile file;
    file.setBuffer(std::string(length, '\0'));
    Settings defaults;
    Settings decoded;
    decoded.orientation = 77;
    expectStatus(runner, SettingsReadStatus::Truncated, readSettingsFile(file, defaults, decoded),
                 "mandatory_header_truncated_" + std::to_string(length));
    runner.expectEq<uint8_t>(77, decoded.orientation, "truncated_output_unchanged_" + std::to_string(length));
  }

  {
    auto file = headerFile(0x12345678, kSettingsFileVersion, 0);
    Settings defaults;
    Settings decoded;
    expectStatus(runner, SettingsReadStatus::InvalidMagic, readSettingsFile(file, defaults, decoded),
                 "invalid_magic_rejected");
  }

  {
    auto file = headerFile(kSettingsMagic, kSettingsFileVersion + 1, 0);
    Settings defaults;
    Settings decoded;
    expectStatus(runner, SettingsReadStatus::UnsupportedVersion, readSettingsFile(file, defaults, decoded),
                 "unsupported_version_rejected");
  }

  {
    auto file = headerFile(kSettingsMagic, kSettingsFileVersion, 0);
    Settings defaults;
    defaults.orientation = Settings::LandscapeCW;
    strcpy(defaults.themeName, "custom");
    Settings decoded;
    expectStatus(runner, SettingsReadStatus::Ok, readSettingsFile(file, defaults, decoded), "zero_fields_accepted");
    runner.expectEq<uint8_t>(Settings::LandscapeCW, decoded.orientation, "zero_fields_preserve_default");
    runner.expectEqual("custom", decoded.themeName, "zero_fields_preserve_string");
  }

  {
    auto file = headerFile(kSettingsMagic, kSettingsFileVersion, 1);
    Settings defaults;
    Settings decoded;
    decoded.orientation = 77;
    expectStatus(runner, SettingsReadStatus::Truncated, readSettingsFile(file, defaults, decoded),
                 "declared_field_truncated");
    runner.expectEq<uint8_t>(77, decoded.orientation, "declared_field_output_unchanged");
  }

  {
    FsFile file;
    file.setBuffer("");
    serialization::writePod(file, kSettingsMagic);
    serialization::writePod(file, uint8_t(7));
    serialization::writePod(file, uint8_t(2));
    serialization::writePod(file, uint8_t(Settings::SleepCover));
    serialization::writePod(file, uint8_t(Settings::LayoutCompact));
    file.seek(0);

    Settings defaults;
    defaults.statusBar = Settings::StatusChapter;
    Settings decoded;
    expectStatus(runner, SettingsReadStatus::Ok, readSettingsFile(file, defaults, decoded),
                 "legacy_short_file_accepted");
    runner.expectEq<uint8_t>(Settings::SleepCover, decoded.sleepScreen, "legacy_sleep_loaded");
    runner.expectEq<uint8_t>(Settings::LayoutCompact, decoded.textLayout, "legacy_layout_loaded");
    runner.expectEq<uint8_t>(Settings::StatusChapter, decoded.statusBar, "legacy_later_default_preserved");
    runner.expectEq<uint8_t>(Settings::FontLarge, decoded.fontSize, "legacy_font_size_migrated");
  }

  {
    FsFile file;
    file.setBuffer("");
    serialization::writePod(file, kSettingsMagic);
    serialization::writePod(file, kSettingsFileVersion);
    serialization::writePod(file, uint8_t(17));
    Settings settings;
    writeFirstSixteenFields(file, settings);
    file.write(reinterpret_cast<const uint8_t*>("bad"), 3);
    file.seek(0);

    Settings decoded;
    decoded.orientation = 77;
    expectStatus(runner, SettingsReadStatus::Truncated, readSettingsFile(file, settings, decoded),
                 "fixed_string_truncated");
    runner.expectEq<uint8_t>(77, decoded.orientation, "fixed_string_output_unchanged");
  }

  {
    Settings expected;
    expected.sleepScreen = Settings::SleepCover;
    expected.textLayout = Settings::LayoutLarge;
    expected.shortPwrBtn = Settings::PowerBookmark;
    expected.statusBar = Settings::StatusChapter;
    expected.orientation = Settings::LandscapeCCW;
    expected.fontSize = Settings::FontLarge;
    expected.pagesPerRefresh = Settings::PPR30;
    expected.sideButtonLayout = Settings::NextPrev;
    expected.autoSleepMinutes = Settings::Sleep30Min;
    expected.paragraphAlignment = Settings::AlignRight;
    expected.hyphenation = 0;
    expected.textAntiAliasing = 1;
    expected.showImages = 0;
    expected.startupBehavior = Settings::StartupHome;
    expected._reserved = 1;
    expected.lineSpacing = Settings::SpacingLarge;
    memset(expected.themeName, 't', sizeof(expected.themeName) - 1);
    expected.themeName[sizeof(expected.themeName) - 1] = '\0';
    memset(expected.lastBookPath, 'b', sizeof(expected.lastBookPath) - 1);
    expected.lastBookPath[sizeof(expected.lastBookPath) - 1] = '\0';
    expected.pendingTransition = 2;
    expected.transitionReturnTo = 2;
    expected.sunlightFadingFix = 1;
    memset(expected.fileListDir, 'd', sizeof(expected.fileListDir) - 1);
    expected.fileListDir[sizeof(expected.fileListDir) - 1] = '\0';
    memset(expected.fileListSelectedName, 'n', sizeof(expected.fileListSelectedName) - 1);
    expected.fileListSelectedName[sizeof(expected.fileListSelectedName) - 1] = '\0';
    expected.fileListSelectedIndex = 54321;
    expected.frontButtonLayout = Settings::FrontLRBC;
    expected.fullBookProcess = 1;
    expected.showRecents = 0;
    expected.recycleBinEnabled = 0;

    FsFile file;
    file.setBuffer("");
    runner.expectTrue(writeSettingsFile(file, expected), "full_settings_write_success");
    file.seek(0);
    Settings decoded;
    expectStatus(runner, SettingsReadStatus::Ok, readSettingsFile(file, Settings{}, decoded),
                 "full_settings_read_success");
    const bool scalarMatch =
        decoded.sleepScreen == expected.sleepScreen && decoded.textLayout == expected.textLayout &&
        decoded.shortPwrBtn == expected.shortPwrBtn && decoded.statusBar == expected.statusBar &&
        decoded.orientation == expected.orientation && decoded.fontSize == expected.fontSize &&
        decoded.pagesPerRefresh == expected.pagesPerRefresh && decoded.sideButtonLayout == expected.sideButtonLayout &&
        decoded.autoSleepMinutes == expected.autoSleepMinutes &&
        decoded.paragraphAlignment == expected.paragraphAlignment && decoded.hyphenation == expected.hyphenation &&
        decoded.textAntiAliasing == expected.textAntiAliasing && decoded.showImages == expected.showImages &&
        decoded.startupBehavior == expected.startupBehavior && decoded._reserved == expected._reserved &&
        decoded.lineSpacing == expected.lineSpacing && decoded.pendingTransition == expected.pendingTransition &&
        decoded.transitionReturnTo == expected.transitionReturnTo &&
        decoded.sunlightFadingFix == expected.sunlightFadingFix &&
        decoded.fileListSelectedIndex == expected.fileListSelectedIndex &&
        decoded.frontButtonLayout == expected.frontButtonLayout &&
        decoded.fullBookProcess == expected.fullBookProcess && decoded.showRecents == expected.showRecents &&
        decoded.recycleBinEnabled == expected.recycleBinEnabled;
    runner.expectTrue(scalarMatch, "full_settings_scalar_roundtrip");
    runner.expectTrue(memcmp(decoded.themeName, expected.themeName, sizeof(expected.themeName)) == 0,
                      "full_settings_theme_roundtrip");
    runner.expectTrue(memcmp(decoded.lastBookPath, expected.lastBookPath, sizeof(expected.lastBookPath)) == 0,
                      "full_settings_book_path_roundtrip");
    runner.expectTrue(memcmp(decoded.fileListDir, expected.fileListDir, sizeof(expected.fileListDir)) == 0,
                      "full_settings_directory_roundtrip");
    runner.expectTrue(
        memcmp(decoded.fileListSelectedName, expected.fileListSelectedName, sizeof(expected.fileListSelectedName)) == 0,
        "full_settings_selected_name_roundtrip");
  }

  {
    Settings previous;
    previous.recycleBinEnabled = 0;
    FsFile file;
    file.setBuffer("");
    runner.expectTrue(writeSettingsFile(file, previous), "previous_settings_fixture_write");
    file.seek(sizeof(kSettingsMagic));
    serialization::writePod(file, uint8_t(13));
    serialization::writePod(file, uint8_t(27));
    file.seek(0);

    Settings defaults;
    defaults.recycleBinEnabled = 1;
    Settings decoded;
    expectStatus(runner, SettingsReadStatus::Ok, readSettingsFile(file, defaults, decoded),
                 "previous_settings_version_accepted");
    runner.expectEq(uint8_t(1), decoded.recycleBinEnabled, "previous_settings_default_recycle_bin_enabled");
  }

  {
    FsFile file;
    file.setBuffer("");
    file.setWriteLimit(1);
    runner.expectFalse(writeSettingsFile(file, Settings{}), "settings_short_write_rejected");
  }

  {
    FsFile file;
    file.setBuffer("");
    file.setSyncResult(false);
    runner.expectFalse(writeSettingsFile(file, Settings{}), "settings_sync_failure_rejected");
  }

  {
    auto file = headerFile(kSettingsMagic, kMinSettingsVersion, 0);
    Settings decoded;
    expectStatus(runner, SettingsReadStatus::Ok, readSettingsFile(file, Settings{}, decoded),
                 "minimum_version_accepted");
  }

  {
    auto file = headerFile(kSettingsMagic, kMinSettingsVersion - 1, 0);
    Settings decoded;
    expectStatus(runner, SettingsReadStatus::UnsupportedVersion, readSettingsFile(file, Settings{}, decoded),
                 "below_minimum_version_rejected");
  }

  {
    FsFile file;
    file.setBuffer("");
    serialization::writePod(file, kSettingsMagic);
    serialization::writePod(file, kSettingsFileVersion);
    serialization::writePod(file, uint8_t(1));
    serialization::writePod(file, uint8_t(0xFF));
    file.seek(0);
    Settings defaults;
    defaults.sleepScreen = Settings::SleepLight;
    Settings decoded;
    expectStatus(runner, SettingsReadStatus::Ok, readSettingsFile(file, defaults, decoded),
                 "out_of_range_field_is_not_truncation");
    runner.expectEq<uint8_t>(Settings::SleepLight, decoded.sleepScreen, "out_of_range_field_preserves_default");
  }

  return runner.allPassed() ? 0 : 1;
}
