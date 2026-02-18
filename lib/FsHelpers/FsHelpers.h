#pragma once
#include <cstring>
#include <string>

class FsHelpers {
 public:
  static std::string normalisePath(const std::string& path);

  // Check if a filename should be hidden from file browsers
  // Note: Does NOT check for "." prefix - caller should check that separately
  static bool isHiddenFsItem(const char* name);

  // Case-insensitive extension check. Extension must include dot (e.g., ".epub").
  static inline bool hasExtension(const char* path, const char* ext) {
    if (!path || !ext) return false;
    const char* pathExt = strrchr(path, '.');
    if (!pathExt) return false;
    return strcasecmp(pathExt, ext) == 0;
  }

  static inline bool hasExtension(const std::string& path, const char* ext) { return hasExtension(path.c_str(), ext); }

  // Image formats
  static inline bool isJpegFile(const char* path) { return hasExtension(path, ".jpg") || hasExtension(path, ".jpeg"); }
  static inline bool isJpegFile(const std::string& path) { return isJpegFile(path.c_str()); }

  static inline bool isPngFile(const char* path) { return hasExtension(path, ".png"); }
  static inline bool isPngFile(const std::string& path) { return isPngFile(path.c_str()); }

  static inline bool isBmpFile(const char* path) { return hasExtension(path, ".bmp"); }
  static inline bool isBmpFile(const std::string& path) { return isBmpFile(path.c_str()); }

  static inline bool isImageFile(const char* path) { return isJpegFile(path) || isPngFile(path) || isBmpFile(path); }
  static inline bool isImageFile(const std::string& path) { return isImageFile(path.c_str()); }

  // Book formats
  static inline bool isEpubFile(const char* path) { return hasExtension(path, ".epub"); }
  static inline bool isEpubFile(const std::string& path) { return isEpubFile(path.c_str()); }

  static inline bool isXtcFile(const char* path) {
    return hasExtension(path, ".xtc") || hasExtension(path, ".xtch") || hasExtension(path, ".xtg") ||
           hasExtension(path, ".xth");
  }
  static inline bool isXtcFile(const std::string& path) { return isXtcFile(path.c_str()); }

  static inline bool isTxtFile(const char* path) { return hasExtension(path, ".txt") || hasExtension(path, ".text"); }
  static inline bool isTxtFile(const std::string& path) { return isTxtFile(path.c_str()); }

  static inline bool isMarkdownFile(const char* path) {
    return hasExtension(path, ".md") || hasExtension(path, ".markdown");
  }
  static inline bool isMarkdownFile(const std::string& path) { return isMarkdownFile(path.c_str()); }

  static inline bool isFb2File(const char* path) { return hasExtension(path, ".fb2"); }
  static inline bool isFb2File(const std::string& path) { return isFb2File(path.c_str()); }

  static inline bool isSupportedBookFile(const char* path) {
    return isEpubFile(path) || isXtcFile(path) || isTxtFile(path) || isMarkdownFile(path) || isFb2File(path);
  }
  static inline bool isSupportedBookFile(const std::string& path) { return isSupportedBookFile(path.c_str()); }
};
