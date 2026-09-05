#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

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
};

struct DmaCoverage {
  DmaCoverageStatus status{DmaCoverageStatus::Empty};
  std::uint64_t requestedBytes{};
  std::uint64_t coveredBytes{};
  std::size_t segmentCount{};
};

[[nodiscard]] DmaCoverage ValidateDmaSegments(std::span<const DmaSegment> segments,
                                              std::uint64_t requestedBytes) noexcept;

} // namespace rtxmac
