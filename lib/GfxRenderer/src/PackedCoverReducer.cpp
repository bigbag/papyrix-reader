#include "PackedCoverReducer.h"

#include <BitmapHelpers.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <new>

namespace home_thumbnail {
namespace {

bool isAbortRequested(const std::function<bool()>& shouldAbort) { return shouldAbort && shouldAbort(); }

bool addWouldOverflow(const size_t left, const size_t right) { return left > SIZE_MAX - right; }

}  // namespace

ReduceResult reducePackedCover(Bitmap& bitmap, FsFile& output, const int destinationWidth, const int destinationHeight,
                               const std::function<bool()>& shouldAbort) {
  const int sourceWidth = bitmap.getWidth();
  const int sourceHeight = bitmap.getHeight();
  const int sourceRowBytes = bitmap.getRowBytes();
  if (!output || bitmap.getBpp() != 1 || !bitmap.isTopDown() || sourceWidth <= 0 || sourceHeight <= 0 ||
      sourceRowBytes <= 0 || destinationWidth <= 0 || destinationHeight <= 0 || destinationWidth > sourceWidth ||
      destinationHeight > sourceHeight) {
    return ReduceResult::Failed;
  }
  if (isAbortRequested(shouldAbort)) return ReduceResult::Cancelled;

  const uint32_t scaleX = static_cast<uint32_t>((static_cast<uint64_t>(sourceWidth) << 16) / destinationWidth);
  const uint32_t scaleY = static_cast<uint32_t>((static_cast<uint64_t>(sourceHeight) << 16) / destinationHeight);
  const size_t maximumSourceRows = static_cast<size_t>((scaleY + 0xFFFFU) >> 16) + 1U;
  const size_t sourceRowSize = static_cast<size_t>(sourceRowBytes);
  if (maximumSourceRows > SIZE_MAX / sourceRowSize) return ReduceResult::Failed;
  const size_t sourceRowsBytes = maximumSourceRows * sourceRowSize;

  const uint64_t outputRowSize64 = (static_cast<uint64_t>(destinationWidth) + 31U) / 32U * 4U;
  if (outputRowSize64 > SIZE_MAX) return ReduceResult::Failed;
  const size_t outputRowBytes = static_cast<size_t>(outputRowSize64);

  const size_t errorElements = static_cast<size_t>(destinationWidth) + 4U;
  if (errorElements > SIZE_MAX / sizeof(int16_t) / 3U) return ReduceResult::Failed;
  const size_t ditherBytes = errorElements * sizeof(int16_t) * 3U;
  if (addWouldOverflow(sourceRowsBytes, outputRowBytes) ||
      addWouldOverflow(sourceRowsBytes + outputRowBytes, ditherBytes)) {
    return ReduceResult::Failed;
  }
  const size_t allocationBytes = sourceRowsBytes + outputRowBytes + ditherBytes;
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (allocationBytes > 1024 && allocationBytes > largest * 80 / 100) return ReduceResult::Failed;

  std::unique_ptr<uint8_t[]> sourceRows(new (std::nothrow) uint8_t[sourceRowsBytes]);
  std::unique_ptr<uint8_t[]> outputRow(new (std::nothrow) uint8_t[outputRowBytes]);
  if (!sourceRows || !outputRow) return ReduceResult::Failed;

  Atkinson1BitDitherer ditherer(destinationWidth);
  if (!ditherer.valid()) return ReduceResult::Failed;
  if (!write1BitBmpHeader(output, destinationWidth, destinationHeight)) return ReduceResult::Failed;
  if (bitmap.rewindToData() != BmpReaderError::Ok) return ReduceResult::Failed;

  for (int destinationY = 0; destinationY < destinationHeight; ++destinationY) {
    if (isAbortRequested(shouldAbort)) return ReduceResult::Cancelled;

    const int sourceYStart =
        static_cast<int>((static_cast<uint64_t>(destinationY) * static_cast<uint64_t>(scaleY)) >> 16);
    int sourceYEnd =
        destinationY + 1 == destinationHeight
            ? sourceHeight
            : static_cast<int>((static_cast<uint64_t>(destinationY + 1) * static_cast<uint64_t>(scaleY)) >> 16);
    if (sourceYEnd <= sourceYStart) sourceYEnd = sourceYStart + 1;
    sourceYEnd = std::min(sourceYEnd, sourceHeight);
    const size_t sourceRowsNeeded = static_cast<size_t>(sourceYEnd - sourceYStart);
    if (sourceRowsNeeded == 0 || sourceRowsNeeded > maximumSourceRows) return ReduceResult::Failed;

    for (int sourceY = sourceYStart; sourceY < sourceYEnd; ++sourceY) {
      if (isAbortRequested(shouldAbort)) return ReduceResult::Cancelled;
      const size_t slot = static_cast<size_t>(sourceY - sourceYStart) * sourceRowSize;
      if (bitmap.readRawRow(sourceRows.get() + slot, sourceRowSize, sourceY) != BmpReaderError::Ok) {
        return ReduceResult::Failed;
      }
    }

    memset(outputRow.get(), 0xFF, outputRowBytes);
    for (int destinationX = 0; destinationX < destinationWidth; ++destinationX) {
      const int sourceXStart =
          static_cast<int>((static_cast<uint64_t>(destinationX) * static_cast<uint64_t>(scaleX)) >> 16);
      int sourceXEnd =
          destinationX + 1 == destinationWidth
              ? sourceWidth
              : static_cast<int>((static_cast<uint64_t>(destinationX + 1) * static_cast<uint64_t>(scaleX)) >> 16);
      if (sourceXEnd <= sourceXStart) sourceXEnd = sourceXStart + 1;
      sourceXEnd = std::min(sourceXEnd, sourceWidth);

      uint32_t sum = 0;
      uint32_t count = 0;
      for (int sourceY = sourceYStart; sourceY < sourceYEnd; ++sourceY) {
        const uint8_t* row = sourceRows.get() + static_cast<size_t>(sourceY - sourceYStart) * sourceRowSize;
        for (int sourceX = sourceXStart; sourceX < sourceXEnd; ++sourceX) {
          const uint8_t index = (row[sourceX >> 3] >> (7 - (sourceX & 7))) & 0x01U;
          sum += bitmap.getPaletteLuminance(index);
          ++count;
        }
      }
      if (count == 0) return ReduceResult::Failed;

      const uint8_t white =
          ditherer.processPixelWithoutAdjustment(static_cast<int>((sum + count / 2U) / count), destinationX);
      if (!white) {
        outputRow[destinationX >> 3] &= static_cast<uint8_t>(~(0x80U >> (destinationX & 7)));
      }
    }

    ditherer.nextRow();
    if (isAbortRequested(shouldAbort)) return ReduceResult::Cancelled;
    if (output.write(outputRow.get(), outputRowBytes) != outputRowBytes) return ReduceResult::Failed;
  }

  return ReduceResult::Ready;
}

}  // namespace home_thumbnail
