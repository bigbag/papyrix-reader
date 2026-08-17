/**
 * XtcParser.cpp
 *
 * XTC file parsing implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "XtcParser.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#define TAG "XTC_PARSE"
#include <SDCardManager.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <new>
#include <utility>

namespace xtc {
namespace {

bool metadataRangeIsValid(const XtcHeader& header, uint64_t fileSize, uint64_t relativeOffset, size_t size) {
  if (header.metadataOffset < sizeof(XtcHeader) || header.metadataOffset > fileSize) return false;
  if (relativeOffset > fileSize - header.metadataOffset) return false;
  const uint64_t absoluteOffset = header.metadataOffset + relativeOffset;
  return size <= fileSize - absoluteOffset;
}

bool readExact(FsFile& file, void* data, size_t size) {
  if (size > static_cast<size_t>(INT_MAX)) return false;
  const int bytesRead = file.read(static_cast<uint8_t*>(data), size);
  return bytesRead >= 0 && static_cast<size_t>(bytesRead) == size;
}

}  // namespace

XtcParser::XtcParser()
    : m_isOpen(false),
      m_defaultWidth(DISPLAY_WIDTH),
      m_defaultHeight(DISPLAY_HEIGHT),
      m_bitDepth(1),
      m_hasChapters(false),
      m_chaptersLoaded(false),
      m_lastError(XtcError::OK) {
  memset(&m_header, 0, sizeof(m_header));
}

XtcParser::~XtcParser() { close(); }

XtcError XtcParser::open(const char* filepath) {
  close();

  // Open file
  if (!SdMan.openFileForRead("XTC", filepath, m_file)) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return m_lastError;
  }

  // Read header
  m_lastError = readHeader();
  if (m_lastError != XtcError::OK) {
    LOG_ERR(TAG, "Failed to read header: %s", errorToString(m_lastError));
    m_file.close();
    return m_lastError;
  }

  if (m_header.hasMetadata) {
    readTitle();
    readAuthor();
  } else {
    m_title.clear();
    m_author.clear();
  }

  // Read page table
  m_lastError = readPageTable();
  if (m_lastError != XtcError::OK) {
    LOG_ERR(TAG, "Failed to read page table: %s", errorToString(m_lastError));
    m_file.close();
    return m_lastError;
  }

  m_hasChapters = m_header.hasChapters == 1 && m_header.chapterOffset != 0;
  m_chaptersLoaded = false;

  m_isOpen = true;
  LOG_INF(TAG, "Opened file: %s (%u pages, %dx%d)", filepath, m_header.pageCount, m_defaultWidth, m_defaultHeight);
  return XtcError::OK;
}

void XtcParser::close() {
  if (m_file) m_file.close();
  m_isOpen = false;
  std::vector<ChapterInfo>().swap(m_chapters);
  m_title.clear();
  m_author.clear();
  m_hasChapters = false;
  m_chaptersLoaded = false;
  memset(&m_header, 0, sizeof(m_header));
}

XtcError XtcParser::readHeader() {
  // Read first 56 bytes of header
  if (!readExact(m_file, &m_header, sizeof(XtcHeader))) {
    return XtcError::READ_ERROR;
  }

  // Verify magic number (accept both XTC and XTCH)
  if (m_header.magic != XTC_MAGIC && m_header.magic != XTCH_MAGIC) {
    LOG_ERR(TAG, "Invalid magic: 0x%08X (expected 0x%08X or 0x%08X)", m_header.magic, XTC_MAGIC, XTCH_MAGIC);
    return XtcError::INVALID_MAGIC;
  }

  // Determine bit depth from file magic
  m_bitDepth = (m_header.magic == XTCH_MAGIC) ? 2 : 1;

  // Check version
  // Currently, version 1.0 is the only valid version, however some generators are swapping the bytes around, so we
  // accept both 1.0 and 0.1 for compatibility
  const bool validVersion = m_header.versionMajor == 1 && m_header.versionMinor == 0 ||
                            m_header.versionMajor == 0 && m_header.versionMinor == 1;
  if (!validVersion) {
    LOG_ERR(TAG, "Unsupported version: %u.%u", m_header.versionMajor, m_header.versionMinor);
    return XtcError::INVALID_VERSION;
  }

  // Basic validation
  if (m_header.pageCount == 0) {
    return XtcError::CORRUPTED_HEADER;
  }

  if (m_header.pageCount > MAX_XTC_PAGE_COUNT) {
    LOG_ERR(TAG, "Page count %u exceeds max %u", m_header.pageCount, MAX_XTC_PAGE_COUNT);
    return XtcError::CORRUPTED_HEADER;
  }

  LOG_INF(TAG, "Header: magic=0x%08X (%s), ver=%u.%u, pages=%u, bitDepth=%u", m_header.magic,
          (m_header.magic == XTCH_MAGIC) ? "XTCH" : "XTC", m_header.versionMajor, m_header.versionMinor,
          m_header.pageCount, m_bitDepth);

  return XtcError::OK;
}

XtcError XtcParser::readTitle() {
  if (!metadataRangeIsValid(m_header, m_file.size(), METADATA_TITLE_OFFSET, METADATA_TITLE_SIZE) ||
      !m_file.seek(m_header.metadataOffset + METADATA_TITLE_OFFSET)) {
    m_title.clear();
    return XtcError::OK;
  }

  char titleBuf[METADATA_TITLE_SIZE] = {0};
  if (!readExact(m_file, titleBuf, METADATA_TITLE_SIZE - 1)) {
    m_title.clear();
    return XtcError::OK;
  }
  m_title.assign(titleBuf, strnlen(titleBuf, METADATA_TITLE_SIZE - 1));
  return XtcError::OK;
}

XtcError XtcParser::readAuthor() {
  if (!metadataRangeIsValid(m_header, m_file.size(), METADATA_AUTHOR_OFFSET, METADATA_AUTHOR_SIZE) ||
      !m_file.seek(m_header.metadataOffset + METADATA_AUTHOR_OFFSET)) {
    m_author.clear();
    return XtcError::OK;
  }

  char authorBuf[METADATA_AUTHOR_SIZE] = {0};
  if (!readExact(m_file, authorBuf, METADATA_AUTHOR_SIZE - 1)) {
    m_author.clear();
    return XtcError::OK;
  }
  m_author.assign(authorBuf, strnlen(authorBuf, METADATA_AUTHOR_SIZE - 1));
  return XtcError::OK;
}

XtcError XtcParser::readPageTable() {
  constexpr uint64_t minPageTableOffset = offsetof(XtcHeader, chapterOffset);
  if (m_header.pageTableOffset < minPageTableOffset) {
    LOG_ERR(TAG, "Page table offset is before the header boundary");
    return XtcError::CORRUPTED_HEADER;
  }

  const uint64_t fileSize = m_file.size();
  const uint64_t pageTableSize = static_cast<uint64_t>(m_header.pageCount) * sizeof(PageTableEntry);
  if (m_header.pageTableOffset > fileSize || pageTableSize > fileSize - m_header.pageTableOffset) {
    LOG_ERR(TAG, "Page table extends beyond file");
    return XtcError::CORRUPTED_HEADER;
  }

  // Read first page entry to get default dimensions
  PageInfo firstPage;
  if (!readPageEntry(0, firstPage)) {
    LOG_ERR(TAG, "Failed to read first page table entry");
    return XtcError::READ_ERROR;
  }
  m_defaultWidth = firstPage.width;
  m_defaultHeight = firstPage.height;

  LOG_INF(TAG, "Validated page table: %u pages (lazy loading)", m_header.pageCount);
  return XtcError::OK;
}

bool XtcParser::readPageEntry(uint32_t pageIndex, PageInfo& info) {
  if (pageIndex >= m_header.pageCount) {
    return false;
  }

  const uint64_t offset = m_header.pageTableOffset + static_cast<uint64_t>(pageIndex) * sizeof(PageTableEntry);
  if (!m_file.seek(offset)) {
    return false;
  }

  PageTableEntry entry;
  if (!readExact(m_file, &entry, sizeof(PageTableEntry))) {
    return false;
  }

  info.offset = entry.dataOffset;
  info.size = entry.dataSize;
  info.width = entry.width;
  info.height = entry.height;
  info.bitDepth = m_bitDepth;
  info.padding = 0;
  return true;
}

bool XtcParser::hasChapters() const {
  ensureChaptersLoaded();
  return m_hasChapters;
}

const std::vector<ChapterInfo>& XtcParser::getChapters() const {
  ensureChaptersLoaded();
  return m_chapters;
}

void XtcParser::ensureChaptersLoaded() const {
  if (m_chaptersLoaded) return;
  m_chaptersLoaded = true;
  const XtcError error = readChapters();
  if (error != XtcError::OK) {
    std::vector<ChapterInfo>().swap(m_chapters);
    m_hasChapters = false;
    LOG_ERR(TAG, "Optional chapters unavailable: %s", errorToString(error));
  }
}

XtcError XtcParser::readChapters() const {
  std::vector<ChapterInfo>().swap(m_chapters);
  m_hasChapters = false;

  if (m_header.hasChapters != 1 || m_header.chapterOffset == 0) return XtcError::OK;

  constexpr uint64_t chapterSize = 96;
  constexpr size_t maxChapters = 500;
  const uint64_t chapterOffset = m_header.chapterOffset;
  const uint64_t fileSize = m_file.size();
  if (chapterOffset < sizeof(XtcHeader) || chapterOffset > fileSize || chapterSize > fileSize - chapterOffset) {
    return XtcError::OK;
  }

  uint64_t maxOffset = fileSize;
  if (m_header.pageTableOffset > chapterOffset && m_header.pageTableOffset <= fileSize) {
    maxOffset = m_header.pageTableOffset;
  } else if (m_header.dataOffset > chapterOffset && m_header.dataOffset <= fileSize) {
    maxOffset = m_header.dataOffset;
  }
  if (maxOffset <= chapterOffset) return XtcError::OK;

  size_t chapterCount = static_cast<size_t>(std::min<uint64_t>((maxOffset - chapterOffset) / chapterSize, maxChapters));
  const bool hasMetadataChapterCount =
      m_header.hasMetadata &&
      metadataRangeIsValid(m_header, fileSize, METADATA_CHAPTER_COUNT_OFFSET, sizeof(uint16_t)) &&
      chapterOffset >= m_header.metadataOffset + METADATA_CHAPTER_COUNT_OFFSET + sizeof(uint16_t);
  if (hasMetadataChapterCount) {
    uint16_t declaredCount = 0;
    if (!m_file.seek(m_header.metadataOffset + METADATA_CHAPTER_COUNT_OFFSET) ||
        !readExact(m_file, &declaredCount, sizeof(declaredCount))) {
      return XtcError::READ_ERROR;
    }
    if (declaredCount > 0) chapterCount = std::min<size_t>(chapterCount, declaredCount);
  }
  if (chapterCount == 0 || !m_file.seek(chapterOffset)) return XtcError::OK;

  const size_t estimatedMemory = chapterCount * (sizeof(ChapterInfo) + 80);
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (estimatedMemory > largestBlock * 80 / 100) return XtcError::MEMORY_ERROR;

  m_chapters.reserve(chapterCount);
  std::array<uint8_t, chapterSize> chapterBuf{};
  for (size_t i = 0; i < chapterCount; ++i) {
    if (!readExact(m_file, chapterBuf.data(), chapterBuf.size())) return XtcError::READ_ERROR;

    const size_t nameLen = strnlen(reinterpret_cast<const char*>(chapterBuf.data()), 80);
    uint16_t startPage = 0;
    uint16_t endPage = 0;
    memcpy(&startPage, chapterBuf.data() + 0x50, sizeof(startPage));
    memcpy(&endPage, chapterBuf.data() + 0x52, sizeof(endPage));
    if (nameLen == 0 && startPage == 0 && endPage == 0) break;
    if (startPage > 0) --startPage;
    if (endPage > 0) --endPage;
    if (startPage >= m_header.pageCount) continue;
    if (endPage >= m_header.pageCount) endPage = m_header.pageCount - 1;
    if (startPage > endPage) continue;

    ChapterInfo chapter;
    chapter.name.assign(reinterpret_cast<const char*>(chapterBuf.data()), nameLen);
    chapter.startPage = startPage;
    chapter.endPage = endPage;
    m_chapters.push_back(std::move(chapter));
  }

  m_hasChapters = !m_chapters.empty();
  LOG_INF(TAG, "Chapters: %u", static_cast<unsigned int>(m_chapters.size()));
  return XtcError::OK;
}

bool XtcParser::readValidatedPage(uint32_t pageIndex, PageInfo& page, XtgPageHeader& pageHeader, size_t& bitmapSize) {
  if (!readPageEntry(pageIndex, page)) {
    m_lastError = XtcError::READ_ERROR;
    return false;
  }

  if (page.width == 0 || page.height == 0 || page.width > MAX_XTC_DIMENSION || page.height > MAX_XTC_DIMENSION) {
    m_lastError = XtcError::CORRUPTED_HEADER;
    return false;
  }

  const uint64_t fileSize = m_file.size();
  if (page.offset > fileSize || sizeof(XtgPageHeader) > fileSize - page.offset || !m_file.seek(page.offset)) {
    m_lastError = XtcError::READ_ERROR;
    return false;
  }

  if (!readExact(m_file, &pageHeader, sizeof(pageHeader))) {
    m_lastError = XtcError::READ_ERROR;
    return false;
  }

  const uint32_t expectedMagic = m_bitDepth == 2 ? XTH_MAGIC : XTG_MAGIC;
  if (pageHeader.magic != expectedMagic) {
    m_lastError = XtcError::INVALID_MAGIC;
    return false;
  }

  if (pageHeader.compression != 0 || pageHeader.width != page.width || pageHeader.height != page.height) {
    m_lastError = XtcError::CORRUPTED_HEADER;
    return false;
  }

  bitmapSize = m_bitDepth == 2 ? xthBitmapSize(page.width, page.height) : xtgBitmapSize(page.width, page.height);
  if (bitmapSize == 0 || bitmapSize > XTC_MAX_BITMAP_SIZE || pageHeader.dataSize != bitmapSize) {
    m_lastError = XtcError::CORRUPTED_HEADER;
    return false;
  }

  const uint64_t totalSize = sizeof(XtgPageHeader) + bitmapSize;
  if (page.size < totalSize || totalSize > fileSize - page.offset) {
    m_lastError = XtcError::CORRUPTED_HEADER;
    return false;
  }

  return true;
}

bool XtcParser::getPageInfo(uint32_t pageIndex, PageInfo& info) { return readPageEntry(pageIndex, info); }

size_t XtcParser::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) {
  if (!m_isOpen) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return 0;
  }
  if (pageIndex >= m_header.pageCount) {
    m_lastError = XtcError::PAGE_OUT_OF_RANGE;
    return 0;
  }
  if (!buffer && bufferSize != 0) {
    m_lastError = XtcError::MEMORY_ERROR;
    return 0;
  }

  PageInfo page;
  XtgPageHeader pageHeader;
  size_t bitmapSize = 0;
  if (!readValidatedPage(pageIndex, page, pageHeader, bitmapSize)) {
    return 0;
  }

  if (bufferSize < bitmapSize) {
    m_lastError = XtcError::MEMORY_ERROR;
    return 0;
  }

  const int bytesRead = m_file.read(buffer, bitmapSize);
  if (bytesRead < 0 || static_cast<size_t>(bytesRead) != bitmapSize) {
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  m_lastError = XtcError::OK;
  return bytesRead;
}

XtcError XtcParser::loadPageStreaming(uint32_t pageIndex, PageChunkCallback callback, size_t chunkSize,
                                      const std::function<bool()>& shouldAbort) {
  if (!m_isOpen) return XtcError::FILE_NOT_FOUND;
  if (pageIndex >= m_header.pageCount) return XtcError::PAGE_OUT_OF_RANGE;
  if (!callback || chunkSize == 0) return XtcError::CORRUPTED_HEADER;

  PageInfo page;
  XtgPageHeader pageHeader;
  size_t bitmapSize = 0;
  if (!readValidatedPage(pageIndex, page, pageHeader, bitmapSize)) {
    return m_lastError;
  }

  chunkSize = std::min<size_t>(chunkSize, 4096);
  if (chunkSize > 1024 && chunkSize > heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) * 80 / 100) {
    return XtcError::MEMORY_ERROR;
  }
  std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[chunkSize]);
  if (!chunk) return XtcError::MEMORY_ERROR;

  size_t totalRead = 0;
  while (totalRead < bitmapSize) {
    if (shouldAbort && shouldAbort()) {
      m_lastError = XtcError::CANCELLED;
      return m_lastError;
    }
    const size_t toRead = std::min(chunkSize, bitmapSize - totalRead);
    const int bytesRead = m_file.read(chunk.get(), toRead);
    if (bytesRead < 0 || static_cast<size_t>(bytesRead) != toRead) {
      m_lastError = XtcError::READ_ERROR;
      return m_lastError;
    }

    if (!callback(chunk.get(), static_cast<size_t>(bytesRead), totalRead)) {
      m_lastError = XtcError::READ_ERROR;
      return m_lastError;
    }
    totalRead += static_cast<size_t>(bytesRead);
  }

  m_lastError = XtcError::OK;
  return m_lastError;
}

bool XtcParser::isValidXtcFile(const char* filepath) {
  FsFile file;
  if (!SdMan.openFileForRead("XTC", filepath, file)) {
    return false;
  }

  uint32_t magic = 0;
  size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic));
  file.close();

  if (bytesRead != sizeof(magic)) {
    return false;
  }

  return (magic == XTC_MAGIC || magic == XTCH_MAGIC);
}

}  // namespace xtc
