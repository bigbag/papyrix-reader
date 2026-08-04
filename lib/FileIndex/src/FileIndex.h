#pragma once

#include <SdFat.h>

#include <cstddef>
#include <cstdint>

class FileIndex {
 public:
  static constexpr size_t MAX_NAME = 255;
  using AcceptFn = bool (*)(const char* name, bool isDir);

  struct Entry {
    char name[MAX_NAME + 1];
    bool isDir;
  };

  FileIndex() = default;
  ~FileIndex() { close(); }
  FileIndex(const FileIndex&) = delete;
  FileIndex& operator=(const FileIndex&) = delete;

  bool open(const char* directory, AcceptFn accept);
  void close();
  size_t size() const { return opened_ ? header_.entryCount : 0; }
  bool entryAt(size_t row, Entry& out);
  size_t findRowByName(const char* name);

 private:
#pragma pack(push, 1)
  struct Header {
    char magic[4];
    uint8_t version;
    uint8_t reserved;
    uint16_t pathLength;
    uint32_t directorySignature;
    uint32_t recordsChecksum;
    uint32_t entryCount;
    uint32_t recordsOffset;
  };

  struct Record {
    uint8_t flags;
    char name[MAX_NAME + 1];
  };
#pragma pack(pop)

  struct BuildState;

  bool scanDirectory(const char* directory, AcceptFn accept, uint32_t& signature, uint32_t& count);
  bool loadExisting(const char* directory, uint32_t signature, uint32_t count);
  bool build(const char* directory, AcceptFn accept);
  bool flushChunk(BuildState& state);
  bool mergeRuns(BuildState& state, uint32_t recordCount, const char*& finalPath);
  bool readRecordAt(size_t row, Record& record);
  static int compareRecords(const Record& left, const Record& right);

  FsFile indexFile_;
  Header header_{};
  bool opened_ = false;
  char indexPath_[64] = {};
  char nameBuffer_[MAX_NAME + 1] = {};
};
