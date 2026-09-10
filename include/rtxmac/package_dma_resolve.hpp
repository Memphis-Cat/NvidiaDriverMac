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

struct ResolvedPackageDmaEntry {
  rtxmac::nvidia::gsp::AllocationKind kind{};
  rtxmac::nvidia::gsp::DmaLayoutRequirement layout{
      rtxmac::nvidia::gsp::DmaLayoutRequirement::None};
  std::uint64_t baseAddress{};
  std::uint64_t allocationBytes{};
  std::uint64_t pageCount{};
};

// Non-owning/non-allocating resolution result suitable for DriverKit cold paths.
// It validates the complete five-section physical layout and records only the
// stable metadata required by later planning. It performs no device access.
struct ResolvedPackageDmaSummary {
  DmaResolveStatus status{DmaResolveStatus::BadPlan};
  std::array<ResolvedPackageDmaEntry, kSectionCount> allocations{};
  std::uint64_t totalPages{};
};

[[nodiscard]] ResolvedPackageDmaSummary ResolvePackageDmaSummary(
    const DmaStagingPlan& plan,
    std::span<const StagedSectionPhysicalView> staged) noexcept;

struct ResolvedPackageDma {
  DmaResolveStatus status{DmaResolveStatus::BadPlan};
  std::array<rtxmac::nvidia::gsp::ResolvedDmaAllocation, kSectionCount>
      allocations{};
  std::uint64_t totalPages{};
};

// Owning portable/offline form. The returned vectors copy each validated page
// list, so allocation failure is permitted to propagate and this is not noexcept.
[[nodiscard]] ResolvedPackageDma ResolvePackageDma(
    const DmaStagingPlan& plan,
    std::span<const StagedSectionPhysicalView> staged);

[[nodiscard]] const char* DmaResolveStatusName(DmaResolveStatus status) noexcept;

} // namespace rtxmac::nvidia::package
