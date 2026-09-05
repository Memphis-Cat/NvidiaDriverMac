#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rtxmac::nvidia::gsp {

inline constexpr std::uint64_t kRadixPageBytes = 4096ull;
inline constexpr std::uint64_t kRadixEntriesPerPage = 512ull;

struct Radix3Layout {
  // [root, level1, level2, image]
  std::array<std::uint64_t, 4> pageCounts{};
  std::array<std::uint64_t, 4> offsets{};
  std::uint64_t tableBytes{};
  std::uint64_t imageBytes{};
  std::uint64_t allocationBytes{};
  std::uint64_t allocationPages{};
};

[[nodiscard]] std::optional<Radix3Layout> PlanRadix3(std::uint64_t imageBytes) noexcept;

// physicalPages must contain one 4K-aligned physical address for every page
// in the complete allocation (tables followed by image). The returned vector
// contains only the table prefix [0, tableBytes); caller copies the image at
// layout.offsets[3].
[[nodiscard]] std::optional<std::vector<std::uint8_t>> BuildRadix3Tables(
    const Radix3Layout& layout,
    std::span<const std::uint64_t> physicalPages) noexcept;

// Build the exact page-aligned host allocation: Radix3 table prefix followed
// by the firmware image, with the final partial image page zero padded.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> BuildRadix3AllocationImage(
    const Radix3Layout& layout,
    std::span<const std::uint64_t> physicalPages,
    std::span<const std::uint8_t> firmwareImage) noexcept;

} // namespace rtxmac::nvidia::gsp
