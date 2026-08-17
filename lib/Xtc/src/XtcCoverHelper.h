#pragma once

#include <Xtc/XtcParser.h>

#include <cstdint>
#include <functional>
#include <string>

namespace xtc {

enum class CoverResult : uint8_t {
  Generated,         // cover.bmp published
  TransientFailure,  // heap/IO/abort — retry later, never persist a failure marker
  InvalidFile,       // deterministic data problem — persist .cover.failed
};

// Generate 1-bit BMP cover from page 0 of an XTC/XTCH file using bounded
// streaming scratch (no page-sized allocations). Caller must ensure the parent
// directory exists.
CoverResult generateCoverBmpFromParser(XtcParser& parser, const std::string& coverBmpPath,
                                       const std::function<bool()>& shouldAbort = nullptr);

// One-time invalidation of failure markers persisted by the old
// page-buffer cover generator (structural 96KB allocation failure), and of
// cover/thumb artifacts produced by the short-lived >=2 threshold build.
// Idempotent: guarded by a .cover.v2 sentinel in the cache directory. When
// purgeArtifacts is set (2-bit source), stale cover.bmp/thumb.bmp are removed
// so the restored threshold regenerates them.
void migrateStaleFailureMarkers(const std::string& cachePath, bool purgeArtifacts = false);

}  // namespace xtc
