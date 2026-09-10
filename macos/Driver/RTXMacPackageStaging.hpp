#pragma once

#include "RTXMacDma.hpp"
#include "rtxmac/boot_package.hpp"
#include "rtxmac/package_dma_plan.hpp"
#include "rtxmac/package_dma_resolve.hpp"

#include <PCIDriverKit/PCIDriverKit.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// DriverKit-only cold staging state for one already verified .rtxpkg. This
// allocates/prepares SYSRAM and records DMA page addresses, but performs no GPU
// MMIO, reset, PRAMIN write, Falcon execution, or GSP start.
enum class RTXMacPackageStageStatus : std::uint32_t {
  Idle = 0u,
  Ok,
  BadArgument,
  PlanRejected,
  SectionLookupFailed,
  DmaAllocationFailed,
  DmaPopulationFailed,
  PageAddressAllocationFailed,
  PageAddressValidationFailed,
  DmaLayoutRejected,
  DmaResolveFailed,
};

struct RTXMacStagedPackageSection {
  rtxmac::nvidia::package::SectionKind kind{};
  rtxmac::nvidia::package::DmaSectionLayout layout{
      rtxmac::nvidia::package::DmaSectionLayout::PageList};
  std::uint64_t logicalBytes{};
  std::uint64_t allocationBytes{};
  RTXMacPreparedDmaBuffer dma{};
  std::uint64_t* pageAddresses{nullptr};
  std::uint32_t pageCount{};
};

struct RTXMacStagedPackage {
  bool ready{};
  RTXMacPackageStageStatus status{RTXMacPackageStageStatus::Idle};
  rtxmac::nvidia::package::DmaStagingPlanStatus planStatus{
      rtxmac::nvidia::package::DmaStagingPlanStatus::PackageNotVerified};
  rtxmac::nvidia::package::DmaResolveStatus resolveStatus{
      rtxmac::nvidia::package::DmaResolveStatus::BadPlan};
  kern_return_t ioStatus{kIOReturnSuccess};
  std::uint32_t failedSectionIndex{0xFFFFFFFFu};
  std::uint64_t totalLogicalBytes{};
  std::uint64_t totalAllocationBytes{};
  std::uint64_t resolvedTotalPages{};
  std::array<std::uint64_t, rtxmac::nvidia::package::kSectionCount>
      resolvedBaseAddresses{};
  std::array<RTXMacStagedPackageSection,
             rtxmac::nvidia::package::kSectionCount> sections{};
};

// The caller supplies bytes that have already passed ParseAndVerify(), semantic
// policy, and live PCI identity matching. This function rechecks the structural
// staging preconditions before allocating. Linear-plan sections are additionally
// rejected unless every returned DMA page is physically contiguous. Finally the
// prepared physical layout is passed through the non-allocating portable DMA
// resolver and only its cold metadata summary is retained. out must be
// zero/default initialized or previously produced by this API.
[[nodiscard]] kern_return_t RTXMacStageVerifiedPackage(
    IOPCIDevice* pci,
    std::span<const std::uint8_t> bytes,
    const rtxmac::nvidia::package::PackageView& view,
    RTXMacStagedPackage* out) noexcept;

void RTXMacReleaseStagedPackage(RTXMacStagedPackage* staged) noexcept;

[[nodiscard]] const char* RTXMacPackageStageStatusName(
    RTXMacPackageStageStatus status) noexcept;
