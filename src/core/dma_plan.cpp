#include "rtxmac/dma_plan.hpp"

#include <algorithm>
#include <limits>

namespace rtxmac {

DmaPlan PlanConservativeDmaChunks(std::uint64_t totalBytes,
                                  std::uint64_t pageSize,
                                  std::uint32_t segmentLimit) {
  DmaPlan out{
      .status = DmaPlanStatus::Ok,
      .totalBytes = totalBytes,
      .pageSize = pageSize,
      .segmentLimit = segmentLimit,
  };

  if (pageSize == 0 || segmentLimit == 0) {
    out.status = DmaPlanStatus::InvalidPageSize;
    return out;
  }
  if (pageSize > std::numeric_limits<std::uint64_t>::max() / segmentLimit) {
    out.status = DmaPlanStatus::SizeOverflow;
    return out;
  }

  const std::uint64_t maxChunk = pageSize * segmentLimit;
  std::uint64_t offset = 0;
  while (offset < totalBytes) {
    const std::uint64_t remaining = totalBytes - offset;
    const std::uint64_t length = std::min(maxChunk, remaining);
    out.chunks.push_back({offset, length});
    offset += length;
  }
  return out;
}

} // namespace rtxmac
