#include "rtxmac/dma.hpp"

#include <limits>

namespace rtxmac {

DmaCoverage ValidateDmaSegments(std::span<const DmaSegment> segments,
                                std::uint64_t requestedBytes) noexcept {
  DmaCoverage out{
      .status = DmaCoverageStatus::Ok,
      .requestedBytes = requestedBytes,
      .coveredBytes = 0,
      .segmentCount = segments.size(),
  };

  if (segments.empty()) {
    out.status = DmaCoverageStatus::Empty;
    return out;
  }

  for (const auto& segment : segments) {
    if (segment.length == 0) {
      out.status = DmaCoverageStatus::ZeroLength;
      return out;
    }
    if (segment.address > std::numeric_limits<std::uint64_t>::max() - (segment.length - 1)) {
      out.status = DmaCoverageStatus::AddressOverflow;
      return out;
    }
    if (out.coveredBytes > std::numeric_limits<std::uint64_t>::max() - segment.length) {
      out.status = DmaCoverageStatus::ByteCountOverflow;
      return out;
    }
    out.coveredBytes += segment.length;
  }

  if (out.coveredBytes < requestedBytes) {
    out.status = DmaCoverageStatus::Truncated;
  }
  return out;
}

} // namespace rtxmac
