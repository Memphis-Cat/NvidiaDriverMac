#pragma once

#include "rtxmac/gsp_manifest.hpp"
#include "rtxmac/package_dma_plan.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace rtxmac::nvidia::package {

struct StagedSectionPhysicalView {
  SectionKind kind{};
  DmaSectionLayout layout{DmaSectionLayout::PageList};
  std::uint64_t logicalBytes{};
  std::uint64_t allocationBytes{};
  std::span<const std::uint64_t> pageAddresses{};
};

enum class DmaResolveStatus : std::uint8_t {
  Ok = 0,
  BadPlan,
  WrongSectionCount,
  SectionMismatch,
  BadAllocationSize,
  BadPageCount,
  BadPageAddress,
  LinearLayoutRejected,
};

struct ResolvedPackageDma {
  DmaResolveStatus status{DmaResolveStatus::BadPlan};
  std::array<rtxmac::nvidia::gsp::ResolvedDmaAllocation, kSectionCount>
      allocations{};
  std::uint64_t totalPages{};
};

// Convert the five cold DriverKit staging results into the portable boot
// manifest DMA allocation model. This performs no allocation, MMIO, GPU write,
// reset, firmware execution, or device access.
[[nodiscard]] ResolvedPackageDma ResolvePackageDma(
    const DmaStagingPlan& plan,
    std::span<const StagedSectionPhysicalView> staged) noexcept;

[[nodiscard]] const char* DmaResolveStatusName(DmaResolveStatus status) noexcept;

} // namespace rtxmac::nvidia::package
