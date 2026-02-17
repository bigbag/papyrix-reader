#pragma once
#include <SdFat.h>

#include <string>
#include <unordered_map>
#include <vector>

class ZipFile {
 public:
  static constexpr size_t DECOMP_DICT_SIZE = 32768;  // Must match TINFL_LZ_DICT_SIZE
  struct FileStatSlim {
    uint16_t method;             // Compression method
    uint32_t compressedSize;     // Compressed size
    uint32_t uncompressedSize;   // Uncompressed size
    uint32_t localHeaderOffset;  // Offset of local file header
  };

  struct ZipDetails {
    uint32_t centralDirOffset;
    uint16_t totalEntries;
    bool isSet;
  };

  struct SizeTarget {
    uint64_t hash;   // FNV-1a 64-bit hash of normalized path
    uint16_t len;    // Length for collision reduction
    uint16_t index;  // Caller's index (e.g. spine index)

    bool operator<(const SizeTarget& other) const {
      return hash < other.hash || (hash == other.hash && len < other.len);
    }
  };

  // FNV-1a 64-bit hash (no std::string allocation)
  // Combined with 16-bit length provides ~80 bits of entropy;
  // collision probability negligible for typical EPUB file counts
  static uint64_t fnvHash64(const char* s, size_t len) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
      hash ^= static_cast<uint8_t>(s[i]);
      hash *= 1099511628211ull;
    }
    return hash;
  }

 private:
  const std::string& filePath;
  FsFile file;
  ZipDetails zipDetails = {0, 0, false};
  std::unordered_map<std::string, FileStatSlim> fileStatSlimCache;

  bool loadFileStatSlim(const char* filename, FileStatSlim* fileStat);
  long getDataOffset(const FileStatSlim& fileStat);
  bool loadZipDetails();

 public:
  explicit ZipFile(const std::string& filePath) : filePath(filePath) {}
  ~ZipFile() = default;
  // Zip file can be opened and closed by hand in order to allow for quick calculation of inflated file size
  // It is NOT recommended to pre-open it for any kind of inflation due to memory constraints
  bool isOpen() const { return !!file; }
  bool open();
  bool close();
  bool loadAllFileStatSlims();
  const std::unordered_map<std::string, FileStatSlim>& getFileStatSlimCache() const { return fileStatSlimCache; }
  uint16_t getTotalEntries();
  bool getInflatedFileSize(const char* filename, size_t* size);
  // Batch lookup: scan ZIP central dir once and fill sizes for matching targets.
  // targets must be sorted by (hash, len). sizes[target.index] receives uncompressedSize.
  // Returns number of targets matched.
  int fillUncompressedSizes(std::vector<SizeTarget>& targets, std::vector<uint32_t>& sizes);
  // Find first existing file from a list of paths. Returns index into paths array, or -1 if none found.
  // More efficient than calling getInflatedFileSize() for each path individually.
  int findFirstExisting(const char* const* paths, int pathCount);
  // Due to the memory required to run each of these, it is recommended to not preopen the zip file for multiple
  // These functions will open and close the zip as needed
  uint8_t* readFileToMemory(const char* filename, size_t* size = nullptr, bool trailingNullByte = false);
  bool readFileToStream(const char* filename, Print& out, size_t chunkSize, uint8_t* dictBuffer = nullptr);
};
