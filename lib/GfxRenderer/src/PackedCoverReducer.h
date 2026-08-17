#pragma once

#include <Bitmap.h>
#include <SdFat.h>

#include <functional>

namespace home_thumbnail {

enum class ReduceResult { Ready, Cancelled, Failed };

ReduceResult reducePackedCover(Bitmap& bitmap, FsFile& output, int destinationWidth, int destinationHeight,
                               const std::function<bool()>& shouldAbort = nullptr);

}  // namespace home_thumbnail
