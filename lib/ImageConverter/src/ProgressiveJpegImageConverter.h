#pragma once

#include <TargetConfig.h>

#if PAPYRIX_CAP_PROGRESSIVE_JPEG
#include "ImageConverter.h"

class ProgressiveJpegImageConverter final : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) override;
  const char* formatName() const override { return "Progressive JPEG"; }
};
#endif
