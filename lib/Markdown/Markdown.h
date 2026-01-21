/**
 * Markdown.h
 *
 * Markdown file handler for Papyrix Reader
 * Provides EPUB-like interface for Markdown file handling
 */

#pragma once

#include <cstdint>
#include <string>

/**
 * Markdown File Handler
 *
 * Handles Markdown file loading, content streaming, and cover image discovery.
 * Interface designed to be similar to Epub/Xtc/Txt classes for easy integration.
 */
class Markdown {
  std::string filepath;
  std::string cachePath;
  std::string title;
  size_t fileSize;
  bool loaded;

 public:
  explicit Markdown(std::string filepath, const std::string& cacheDir);
  ~Markdown() = default;

  /**
   * Load Markdown file (verify existence and get size)
   * @return true on success
   */
  bool load();

  /**
   * Clear cached data
   * @return true on success
   */
  bool clearCache() const;

  /**
   * Setup cache directory
   */
  void setupCacheDir() const;

  // Path accessors
  const std::string& getCachePath() const { return cachePath; }
  const std::string& getPath() const { return filepath; }

  // Metadata
  const std::string& getTitle() const { return title; }
  size_t getFileSize() const { return fileSize; }

  // Cover image support (for sleep screen)
  std::string getCoverBmpPath() const;
  bool generateCoverBmp() const;

  /**
   * Read content from file at specified offset
   * @param buffer Output buffer
   * @param offset Byte offset in file
   * @param length Number of bytes to read
   * @return Number of bytes actually read
   */
  size_t readContent(uint8_t* buffer, size_t offset, size_t length) const;

  /**
   * Find a cover image in the same directory as the Markdown file
   * Searches for: <filename>.jpg, <filename>.bmp, cover.jpg, cover.bmp
   * @return Path to cover image, or empty string if not found
   */
  std::string findCoverImage() const;

  // Check if file is loaded
  bool isLoaded() const { return loaded; }
};
