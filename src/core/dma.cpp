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
  } else if (out.coveredBytes > requestedBytes) {
    out.status = DmaCoverageStatus::Overrun;
  }
  return out;
}

DmaPageMap ExpandDmaSegmentsToPages(
    std::span<const DmaSegment> segments,
    std::uint64_t requestedBytes,
    std::uint64_t pageBytes) {
  DmaPageMap out{};
  out.pageBytes = pageBytes;

  if (pageBytes == 0 || (pageBytes & (pageBytes - 1u)) != 0u) {
    out.status = DmaPageMapStatus::InvalidPageSize;
    return out;
  }
  if (requestedBytes == 0 || (requestedBytes % pageBytes) != 0u) {
    out.status = DmaPageMapStatus::RequestedSizeNotPageAligned;
    return out;
  }

  const auto coverage = ValidateDmaSegments(segments, requestedBytes);
  if (coverage.status != DmaCoverageStatus::Ok || coverage.coveredBytes != requestedBytes) {
    out.status = DmaPageMapStatus::CoverageError;
    return out;
  }

  const std::uint64_t pageCount64 = requestedBytes / pageBytes;
  if (pageCount64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    out.status = DmaPageMapStatus::PageCountOverflow;
    return out;
  }

  out.pageAddresses.reserve(static_cast<std::size_t>(pageCount64));
  for (const auto& segment : segments) {
    if ((segment.address % pageBytes) != 0u) {
      out.pageAddresses.clear();
      out.status = DmaPageMapStatus::SegmentAddressUnaligned;
      return out;
    }
    if ((segment.length % pageBytes) != 0u) {
      out.pageAddresses.clear();
      out.status = DmaPageMapStatus::SegmentLengthUnaligned;
      return out;
    }

    for (std::uint64_t offset = 0; offset < segment.length; offset += pageBytes) {
      // ValidateDmaSegments already proved address + length - 1 cannot wrap.
      out.pageAddresses.push_back(segment.address + offset);
    }
  }

  if (out.pageAddresses.size() != static_cast<std::size_t>(pageCount64)) {
    out.pageAddresses.clear();
    out.status = DmaPageMapStatus::CoverageError;
    return out;
  }

  out.status = DmaPageMapStatus::Ok;
  return out;
}

} // namespace rtxmac
