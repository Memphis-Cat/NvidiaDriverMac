#pragma once

#include "RTXMacDma.hpp"
#include "RTXMacPackageStaging.hpp"
#include "rtxmac/ga10x_boot_prepare.hpp"
#include "rtxmac/ga10x_boot_resolve.hpp"
#include "rtxmac/gsp_manifest.hpp"

#include <PCIDriverKit/PCIDriverKit.h>

#include <array>
#include <cstdint>
#include <span>

// Retained DriverKit ownership for a completely constructed, cold GA10x host
// boot-memory graph. "ready" means the buffers and artifacts are internally
// consistent; it does not authorize or perform any GPU write or execution.
enum class RTXMacBootSessionStatus : std::uint32_t {
  Idle = 0u,
  Ok,
  BadArgument,
  PackageNotReady,
  PackageMismatch,
  ProfileRejected,
  PlanRejected,
  PackageSummaryRejected,
  GeneratedAllocationFailed,
  PageAddressAllocationFailed,
  PageAddressValidationFailed,
  GeneratedLayoutRejected,
  AddressResolveFailed,
  SystemInfoFailed,
  ArtifactBuildFailed,
  ArtifactPopulationFailed,
  SequenceRejected,
};

struct RTXMacGeneratedDmaBuffer {
  rtxmac::nvidia::prototype::GeneratedBufferKind kind{};
  rtxmac::nvidia::prototype::GeneratedDmaLayout layout{
      rtxmac::nvidia::prototype::GeneratedDmaLayout::PageList};
  std::uint64_t logicalBytes{};
  std::uint64_t allocationBytes{};
  RTXMacPreparedDmaBuffer dma{};
  std::uint64_t* pageAddresses{nullptr};
  std::uint32_t pageCount{};
};

struct RTXMacColdBootSession {
  bool ready{};
  RTXMacBootSessionStatus status{RTXMacBootSessionStatus::Idle};
  rtxmac::nvidia::prototype::ProfileStatus profileStatus{
      rtxmac::nvidia::prototype::ProfileStatus::PackageNotVerified};
  rtxmac::nvidia::prototype::BootPreparationPlanStatus planStatus{
      rtxmac::nvidia::prototype::BootPreparationPlanStatus::InvalidProfile};
  rtxmac::nvidia::package::DmaResolveStatus packageResolveStatus{
      rtxmac::nvidia::package::DmaResolveStatus::BadPlan};
  rtxmac::nvidia::prototype::BootResolveStatus bootResolveStatus{
      rtxmac::nvidia::prototype::BootResolveStatus::BadProfile};
  kern_return_t ioStatus{kIOReturnSuccess};
  std::uint32_t failedGeneratedIndex{0xFFFFFFFFu};
  std::uint64_t totalLogicalBytes{};
  std::uint64_t totalAllocationBytes{};
  std::uint64_t totalPages{};
  rtxmac::nvidia::gsp::ResolvedAddresses addresses{};
  std::uint32_t bootPhaseCount{};
  bool executableWithCurrentCore{};
  std::array<RTXMacGeneratedDmaBuffer,
             rtxmac::nvidia::prototype::kGeneratedDmaRequirementCount>
      generated{};
};

// The package must be the exact package bound to staged (SHA-256 match). This
// allocates and constructs host DMA artifacts only. It performs no PCI command
// change, BAR/MMIO/PRAMIN write, reset, Falcon execution, or GSP start.
[[nodiscard]] kern_return_t RTXMacPrepareColdBootSession(
    IOPCIDevice* pci,
    std::span<const std::uint8_t> bytes,
    const rtxmac::nvidia::package::PackageView& view,
    const RTXMacStagedPackage& staged,
    RTXMacColdBootSession* out) noexcept;

void RTXMacReleaseColdBootSession(RTXMacColdBootSession* session) noexcept;

[[nodiscard]] const char* RTXMacBootSessionStatusName(
    RTXMacBootSessionStatus status) noexcept;
