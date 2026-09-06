#pragma once

#include "rtxmac/boot_package.hpp"

#include <array>
#include <cstdint>

namespace rtxmac::nvidia::package {

inline constexpr std::uint64_t kPackageDmaPageBytes = 0x1000ull;

enum class DmaSectionLayout : std::uint8_t {
  PageList = 0u,
  Linear,
};

struct DmaSectionPlan {
  SectionKind kind{};
  DmaSectionLayout layout{DmaSectionLayout::PageList};
  std::uint64_t logicalBytes{};
  std::uint64_t allocationBytes{};
  std::uint64_t pageCount{};
};

enum class DmaStagingPlanStatus : std::uint8_t {
  Ok = 0,
  PackageNotVerified,
  MissingOrEmptySection,
  SizeOverflow,
  TotalOverflow,
};

struct DmaStagingPlan {
  DmaStagingPlanStatus status{DmaStagingPlanStatus::PackageNotVerified};
  std::array<DmaSectionPlan, kSectionCount> sections{};
  std::uint64_t totalLogicalBytes{};
  std::uint64_t totalAllocationBytes{};
};

// Build the CPU/SYSRAM allocation plan for an already verified package.
// Every payload gets its own 4 KiB-aligned allocation. allocationBytes may be
// larger than logicalBytes; the tail must be zero-filled before DMA prepare.
// GSP signature and GSP bootloader must resolve to physically linear DMA ranges
// because later Ampere boot metadata addresses each as one contiguous object.
// Other raw package sections may be represented by validated page lists.
[[nodiscard]] DmaStagingPlan PlanPackageDmaStaging(
    const PackageView& view) noexcept;

[[nodiscard]] const char* DmaStagingPlanStatusName(
    DmaStagingPlanStatus status) noexcept;

} // namespace rtxmac::nvidia::package
