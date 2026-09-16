#include "rtxmac/ga10x_boot_prepare.hpp"

#include "rtxmac/frts.hpp"

#include <limits>

namespace rtxmac::nvidia::prototype {
namespace {

using gsp::AllocationKind;
using gsp::AllocationRequirement;
using gsp::DmaLayoutRequirement;
using gsp::MemoryDomain;

const AllocationRequirement* FindAllocation(
    const gsp::BootManifest& manifest,
    AllocationKind kind) noexcept {
  for (const auto& allocation : manifest.allocations) {
    if (allocation.kind == kind) return &allocation;
  }
  return nullptr;
}

std::uint64_t AlignUpPage(std::uint64_t value) noexcept {
  if (value == 0u || value > std::numeric_limits<std::uint64_t>::max() -
                                  (kBootPreparePageBytes - 1u)) {
    return 0u;
  }
  return (value + kBootPreparePageBytes - 1u) & ~(kBootPreparePageBytes - 1u);
}

bool AddRequirement(BootPreparationPlan& out,
                    std::size_t index,
                    GeneratedBufferKind kind,
                    GeneratedDmaLayout layout,
                    std::uint64_t logicalBytes,
                    std::uint64_t allocationBytes) noexcept {
  if (index >= out.generated.size() || logicalBytes == 0u ||
      allocationBytes == 0u || allocationBytes < logicalBytes ||
      (allocationBytes % kBootPreparePageBytes) != 0u) {
    return false;
  }
  if (out.totalGeneratedAllocationBytes >
      std::numeric_limits<std::uint64_t>::max() - allocationBytes) {
    return false;
  }
  out.generated[index] = {
      .kind = kind,
      .layout = layout,
      .logicalBytes = logicalBytes,
      .allocationBytes = allocationBytes,
      .pageCount = allocationBytes / kBootPreparePageBytes,
  };
  out.totalGeneratedAllocationBytes += allocationBytes;
  return true;
}

bool AddManifestRequirement(BootPreparationPlan& out,
                            std::size_t index,
                            GeneratedBufferKind kind,
                            const gsp::BootManifest& manifest,
                            AllocationKind allocationKind,
                            GeneratedDmaLayout expectedLayout) noexcept {
  const AllocationRequirement* requirement = FindAllocation(manifest, allocationKind);
  if (!requirement || requirement->domain != MemoryDomain::System ||
      !requirement->requiresDmaMapping || requirement->logicalBytes == 0u ||
      requirement->allocationBytes == 0u) {
    return false;
  }
  const DmaLayoutRequirement required = expectedLayout == GeneratedDmaLayout::Linear
      ? DmaLayoutRequirement::Linear
      : DmaLayoutRequirement::PageList;
  if (requirement->dmaLayout != required) return false;
  return AddRequirement(out, index, kind, expectedLayout,
                        requirement->logicalBytes,
                        requirement->allocationBytes);
}

} // namespace

BootPreparationPlan PlanGa10xBootPreparation(const Profile& profile) noexcept {
  BootPreparationPlan out{};
  if (profile.status != ProfileStatus::Ok) {
    out.status = BootPreparationPlanStatus::InvalidProfile;
    return out;
  }
  if (!profile.manifest.valid || profile.manifest.allocations.size() != 9u) {
    out.status = BootPreparationPlanStatus::InvalidManifest;
    return out;
  }

  std::size_t index = 0u;
  if (!AddManifestRequirement(out, index++, GeneratedBufferKind::QueueBacking,
                              profile.manifest, AllocationKind::QueueBacking,
                              GeneratedDmaLayout::PageList) ||
      !AddManifestRequirement(out, index++, GeneratedBufferKind::CachedArguments,
                              profile.manifest, AllocationKind::CachedArguments,
                              GeneratedDmaLayout::Linear) ||
      !AddManifestRequirement(out, index++, GeneratedBufferKind::LibosInitArguments,
                              profile.manifest, AllocationKind::LibosInitArguments,
                              GeneratedDmaLayout::Linear) ||
      !AddManifestRequirement(out, index++, GeneratedBufferKind::WprMetadata,
                              profile.manifest, AllocationKind::WprMetadata,
                              GeneratedDmaLayout::Linear) ||
      !AddManifestRequirement(out, index++, GeneratedBufferKind::Radix3Firmware,
                              profile.manifest, AllocationKind::Radix3Firmware,
                              GeneratedDmaLayout::PageList)) {
    out.status = BootPreparationPlanStatus::InvalidManifest;
    return out;
  }

  for (const GeneratedBufferKind kind : {
           GeneratedBufferKind::LogInit,
           GeneratedBufferKind::LogIntr,
           GeneratedBufferKind::LogRm,
           GeneratedBufferKind::LogMnoc,
           GeneratedBufferKind::LogKrnl}) {
    if (!AddRequirement(out, index++, kind, GeneratedDmaLayout::Linear,
                        kLibosLogRegionBytes, kLibosLogRegionBytes)) {
      out.status = BootPreparationPlanStatus::SizeOverflow;
      return out;
    }
  }
  if (index != out.generated.size()) {
    out.status = BootPreparationPlanStatus::InvalidManifest;
    return out;
  }

  const AllocationRequirement* signature =
      FindAllocation(profile.manifest, AllocationKind::FirmwareSignature);
  const AllocationRequirement* bootloader =
      FindAllocation(profile.manifest, AllocationKind::GspBootloader);
  if (!signature || !bootloader || signature->domain != MemoryDomain::System ||
      bootloader->domain != MemoryDomain::System ||
      signature->dmaLayout != DmaLayoutRequirement::Linear ||
      bootloader->dmaLayout != DmaLayoutRequirement::Linear ||
      signature->allocationBytes == 0u || bootloader->allocationBytes == 0u) {
    out.status = BootPreparationPlanStatus::InvalidManifest;
    return out;
  }
  out.reusedSignatureAllocationBytes = signature->allocationBytes;
  out.reusedBootloaderAllocationBytes = bootloader->allocationBytes;

  const std::uint64_t fwsecAllocation =
      AlignUpPage(profile.manifest.inputs.frtsFwsecImageBytes);
  const std::uint64_t sec2Allocation =
      AlignUpPage(profile.manifest.inputs.sec2BooterImageBytes);
  if (fwsecAllocation == 0u || sec2Allocation == 0u) {
    out.status = BootPreparationPlanStatus::SizeOverflow;
    return out;
  }

  const std::uint64_t reservedStart = profile.manifest.wpr.gspFwRsvdStart;
  if (reservedStart < sec2Allocation ||
      reservedStart - sec2Allocation < fwsecAllocation) {
    out.status = BootPreparationPlanStatus::FramebufferScratchUnderflow;
    return out;
  }
  const std::uint64_t sec2Offset = reservedStart - sec2Allocation;
  const std::uint64_t fwsecOffset = sec2Offset - fwsecAllocation;
  if ((sec2Offset % kBootPreparePageBytes) != 0u ||
      (fwsecOffset % kBootPreparePageBytes) != 0u) {
    out.status = BootPreparationPlanStatus::InvalidManifest;
    return out;
  }

  const auto frtsCommand = frts::BuildFrtsCommand(profile.manifest.inputs.fbSize);
  if (!frtsCommand ||
      frtsCommand->regionOffsetBytes != profile.manifest.wpr.frtsOffset ||
      profile.manifest.wpr.frtsSize != kFrtsBytes) {
    out.status = BootPreparationPlanStatus::FrtsPlacementMismatch;
    return out;
  }

  out.framebuffer = {
      .frtsFwsecOffset = fwsecOffset,
      .frtsFwsecBytes = fwsecAllocation,
      .sec2BooterOffset = sec2Offset,
      .sec2BooterBytes = sec2Allocation,
      .frtsRegionOffset = profile.manifest.wpr.frtsOffset,
      .frtsRegionBytes = profile.manifest.wpr.frtsSize,
  };
  out.status = BootPreparationPlanStatus::Ok;
  return out;
}

const char* BootPreparationPlanStatusName(
    BootPreparationPlanStatus status) noexcept {
  switch (status) {
    case BootPreparationPlanStatus::Ok: return "ok";
    case BootPreparationPlanStatus::InvalidProfile: return "invalid-profile";
    case BootPreparationPlanStatus::InvalidManifest: return "invalid-manifest";
    case BootPreparationPlanStatus::SizeOverflow: return "size-overflow";
    case BootPreparationPlanStatus::FramebufferScratchUnderflow:
      return "framebuffer-scratch-underflow";
    case BootPreparationPlanStatus::FrtsPlacementMismatch:
      return "frts-placement-mismatch";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::prototype
