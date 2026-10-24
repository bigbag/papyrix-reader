#pragma once

#include <cstdio>
#include <cstdlib>

#include "content/RecentBooksStore.h"
#include "core/FirmwareUpdater.h"
#include "core/Core.h"
#include "ThemeManager.h"

[[noreturn]] inline void unexpectedSettingsService(const char* name) {
  std::fprintf(stderr, "Unexpected settings service: %s\n", name);
  std::abort();
}

ThemeManager::ThemeManager() : activeTheme(getBuiltinLightTheme()) { std::strcpy(themeName, "light"); }
ThemeManager& ThemeManager::instance() { static ThemeManager manager; return manager; }
std::vector<std::string> ThemeManager::listAvailableThemes(bool) { unexpectedSettingsService("theme files"); }
bool ThemeManager::applyCachedTheme(const char*) { unexpectedSettingsService("theme change"); }
bool ThemeManager::loadTheme(const char*) { unexpectedSettingsService("theme load"); }

namespace papyrix {
ContentHandle::ContentHandle() {}
ContentHandle::~ContentHandle() { if (type != ContentType::None) unexpectedSettingsService("active content"); }
Core core;
FirmwareUpdater& FirmwareUpdater::instance() { static FirmwareUpdater updater; return updater; }
bool FirmwareUpdater::findFirmwareFile(const char*) { unexpectedSettingsService("firmware lookup"); }
bool FirmwareUpdater::beginUpdate() { unexpectedSettingsService("firmware write"); }
bool FirmwareUpdater::pump() { unexpectedSettingsService("firmware pump"); }
void FirmwareUpdater::abort() { unexpectedSettingsService("firmware abort"); }
void FirmwareUpdater::reset() { unexpectedSettingsService("firmware reset"); }
RecentBooksStore& RecentBooksStore::instance() { static RecentBooksStore store; return store; }
bool RecentBooksStore::clearAndSave() { unexpectedSettingsService("recent book removal"); }
namespace hal {
void Input::resyncState() {}
bool FrontLight::setBrightness(uint8_t) { unexpectedSettingsService("frontlight brightness"); }
bool FrontLight::setWarmth(uint8_t) { unexpectedSettingsService("frontlight warmth"); }
Battery::Status Battery::readStatus() const { unexpectedSettingsService("battery read"); }
}
}  // namespace papyrix
