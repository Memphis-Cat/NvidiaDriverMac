#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rtxmac {

struct DmaSegment {
  std::uint64_t address{};
  std::uint64_t length{};
};

enum class DmaCoverageStatus : std::uint8_t {
  Ok = 0,
  Empty,
  ZeroLength,
  AddressOverflow,
  ByteCountOverflow,
  Truncated,
  Overrun,
};

struct DmaCoverage {
  DmaCoverageStatus status{DmaCoverageStatus::Empty};
  std::uint64_t requestedBytes{};
  std::uint64_t coveredBytes{};
  std::size_t segmentCount{};
};

[[nodiscard]] DmaCoverage ValidateDmaSegments(std::span<const DmaSegment> segments,
                                              std::uint64_t requestedBytes) noexcept;

enum class DmaLinearRangeStatus : std::uint8_t {
  Ok = 0,
  CoverageError,
  AddressDiscontinuity,
};

struct DmaLinearRange {
  DmaLinearRangeStatus status{DmaLinearRangeStatus::CoverageError};
  std::uint64_t address{};
  std::uint64_t length{};
};

// Resolve an exact scatter list into one GPU-linear [base, base+length) range.
// Multiple returned segments are accepted only when each starts exactly where
// the previous segment ended. This is required for boot structures whose ABI
// supplies only a single physical/DMA base address plus a byte count.
[[nodiscard]] DmaLinearRange ResolveLinearDmaRange(
    std::span<const DmaSegment> segments,
    std::uint64_t requestedBytes) noexcept;

enum class DmaPageMapStatus : std::uint8_t {
  Ok = 0,
  InvalidPageSize,
  RequestedSizeNotPageAligned,
  CoverageError,
  SegmentAddressUnaligned,
  SegmentLengthUnaligned,
  PageCountOverflow,
};

struct DmaPageMap {
  DmaPageMapStatus status{DmaPageMapStatus::CoverageError};
  std::uint64_t pageBytes{};
  std::vector<std::uint64_t> pageAddresses;
};

// Convert an exact scatter-list mapping into one GPU-visible address per
// logical page. This is intentionally strict for firmware/PTE use: the request,
// every segment start, and every segment length must be page aligned. A DMA
// segment boundary may therefore never split one logical GPU page.
[[nodiscard]] DmaPageMap ExpandDmaSegmentsToPages(
    std::span<const DmaSegment> segments,
    std::uint64_t requestedBytes,
    std::uint64_t pageBytes = 0x1000ull);

} // namespace rtxmac
