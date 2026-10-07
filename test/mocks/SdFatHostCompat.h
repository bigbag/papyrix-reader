#pragma once

// Minimal Arduino type names that the host build of the real SdFat sources
// needs. PrintBasic.h names this type unconditionally; the host build has no
// flash strings, so a declaration satisfies it.
class __FlashStringHelper;
