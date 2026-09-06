#include "RTXMacPackageStaging.hpp"

#include <limits>

namespace {

void SetFailure(RTXMacStagedPackage* staged,
                RTXMacPackageStageStatus status,
                kern_return_t ioStatus,
                std::uint32_t sectionIndex = 0xFFFFFFFFu) noexcept {
  if (!staged) return;
  staged->ready = false;
  staged->status = status;
  staged->ioStatus = ioStatus;
  staged->failedSectionIndex = sectionIndex;
}

void ReleaseAndRestoreFailure(
    RTXMacStagedPackage* staged,
    const rtxmac::nvidia::package::DmaStagingPlan& plan,
    RTXMacPackageStageStatus status,
    kern_return_t ioStatus,
    std::uint32_t sectionIndex) noexcept {
  if (!staged) return;
  RTXMacReleaseStagedPackage(staged);
  staged->status = status;
  staged->ioStatus = ioStatus;
  staged->failedSectionIndex = sectionIndex;
  staged->planStatus = plan.status;
  staged->totalLogicalBytes = plan.totalLogicalBytes;
  staged->totalAllocationBytes = plan.totalAllocationBytes;
}

bool IsLinearPageList(const std::uint64_t* pages,
                      std::uint32_t pageCount) noexcept {
  if (!pages || pageCount == 0u) return false;
  for (std::uint32_t i = 1u; i < pageCount; ++i) {
    const std::uint64_t previous = pages[i - 1u];
    if (previous > std::numeric_limits<std::uint64_t>::max() -
                       kRTXMacDmaPageBytes ||
        pages[i] != previous + kRTXMacDmaPageBytes) {
      return false;
    }
  }
  return true;
}

} // namespace

kern_return_t RTXMacStageVerifiedPackage(
    IOPCIDevice* pci,
    std::span<const std::uint8_t> bytes,
    const rtxmac::nvidia::package::PackageView& view,
    RTXMacStagedPackage* out) noexcept {
  using namespace rtxmac::nvidia::package;

  if (!pci || !out || bytes.empty() || view.status != ParseStatus::Ok) {
    if (out) SetFailure(out, RTXMacPackageStageStatus::BadArgument,
                        kIOReturnBadArgument);
    return kIOReturnBadArgument;
  }

  // Re-staging replaces prior cold state atomically from the caller's point of
  // view: old buffers are released before any new allocation is attempted.
  RTXMacReleaseStagedPackage(out);

  const DmaStagingPlan plan = PlanPackageDmaStaging(view);
  out->planStatus = plan.status;
  out->totalLogicalBytes = plan.totalLogicalBytes;
  out->totalAllocationBytes = plan.totalAllocationBytes;
  if (plan.status != DmaStagingPlanStatus::Ok) {
    SetFailure(out, RTXMacPackageStageStatus::PlanRejected,
               kIOReturnBadArgument);
    return kIOReturnBadArgument;
  }

  for (std::size_t i = 0u; i < kSectionCount; ++i) {
    const DmaSectionPlan& sectionPlan = plan.sections[i];
    RTXMacStagedPackageSection& stagedSection = out->sections[i];
    stagedSection.kind = sectionPlan.kind;
    stagedSection.layout = sectionPlan.layout;
    stagedSection.logicalBytes = sectionPlan.logicalBytes;
    stagedSection.allocationBytes = sectionPlan.allocationBytes;

    const std::span<const std::uint8_t> payload =
        FindSection(bytes, view, sectionPlan.kind);
    if (payload.empty() || payload.size() != sectionPlan.logicalBytes) {
      ReleaseAndRestoreFailure(
          out, plan, RTXMacPackageStageStatus::SectionLookupFailed,
          kIOReturnBadArgument, static_cast<std::uint32_t>(i));
      return kIOReturnBadArgument;
    }

    kern_return_t kr = RTXMacAllocateAndPrepareDmaBuffer(
        pci, sectionPlan.allocationBytes, &stagedSection.dma);
    if (kr != kIOReturnSuccess) {
      ReleaseAndRestoreFailure(
          out, plan, RTXMacPackageStageStatus::DmaAllocationFailed,
          kr, static_cast<std::uint32_t>(i));
      return kr;
    }

    kr = RTXMacCopyIntoPreparedDmaBufferPadded(
        &stagedSection.dma, payload.data(), sectionPlan.logicalBytes);
    if (kr != kIOReturnSuccess) {
      ReleaseAndRestoreFailure(
          out, plan, RTXMacPackageStageStatus::DmaPopulationFailed,
          kr, static_cast<std::uint32_t>(i));
      return kr;
    }

    if (sectionPlan.pageCount == 0u ||
        sectionPlan.pageCount > std::numeric_limits<std::uint32_t>::max()) {
      ReleaseAndRestoreFailure(
          out, plan, RTXMacPackageStageStatus::PageAddressAllocationFailed,
          kIOReturnNoResources, static_cast<std::uint32_t>(i));
      return kIOReturnNoResources;
    }

    const auto expectedPages = static_cast<std::uint32_t>(sectionPlan.pageCount);
    stagedSection.pageAddresses = new std::uint64_t[expectedPages]();
    if (!stagedSection.pageAddresses) {
      ReleaseAndRestoreFailure(
          out, plan, RTXMacPackageStageStatus::PageAddressAllocationFailed,
          kIOReturnNoMemory, static_cast<std::uint32_t>(i));
      return kIOReturnNoMemory;
    }

    std::uint32_t pageCount = 0u;
    kr = RTXMacCollectDmaPageAddresses(
        &stagedSection.dma,
        stagedSection.pageAddresses,
        expectedPages,
        &pageCount);
    if (kr != kIOReturnSuccess || pageCount != expectedPages) {
      const kern_return_t failure =
          kr == kIOReturnSuccess ? kIOReturnError : kr;
      ReleaseAndRestoreFailure(
          out, plan, RTXMacPackageStageStatus::PageAddressValidationFailed,
          failure, static_cast<std::uint32_t>(i));
      return failure;
    }
    stagedSection.pageCount = pageCount;

    if (sectionPlan.layout == DmaSectionLayout::Linear &&
        !IsLinearPageList(stagedSection.pageAddresses,
                          stagedSection.pageCount)) {
      ReleaseAndRestoreFailure(
          out, plan, RTXMacPackageStageStatus::DmaLayoutRejected,
          kIOReturnNoResources, static_cast<std::uint32_t>(i));
      return kIOReturnNoResources;
    }
  }

  out->ready = true;
  out->status = RTXMacPackageStageStatus::Ok;
  out->ioStatus = kIOReturnSuccess;
  out->failedSectionIndex = 0xFFFFFFFFu;
  return kIOReturnSuccess;
}

void RTXMacReleaseStagedPackage(RTXMacStagedPackage* staged) noexcept {
  if (!staged) return;
  for (auto& section : staged->sections) {
    if (section.pageAddresses) {
      delete[] section.pageAddresses;
      section.pageAddresses = nullptr;
    }
    section.pageCount = 0u;
    RTXMacReleasePreparedDmaBuffer(&section.dma);
    section.kind = {};
    section.layout = rtxmac::nvidia::package::DmaSectionLayout::PageList;
    section.logicalBytes = 0u;
    section.allocationBytes = 0u;
  }

  staged->ready = false;
  staged->status = RTXMacPackageStageStatus::Idle;
  staged->planStatus =
      rtxmac::nvidia::package::DmaStagingPlanStatus::PackageNotVerified;
  staged->ioStatus = kIOReturnSuccess;
  staged->failedSectionIndex = 0xFFFFFFFFu;
  staged->totalLogicalBytes = 0u;
  staged->totalAllocationBytes = 0u;
}

const char* RTXMacPackageStageStatusName(
    RTXMacPackageStageStatus status) noexcept {
  switch (status) {
    case RTXMacPackageStageStatus::Idle: return "idle";
    case RTXMacPackageStageStatus::Ok: return "ok";
    case RTXMacPackageStageStatus::BadArgument: return "bad-argument";
    case RTXMacPackageStageStatus::PlanRejected: return "plan-rejected";
    case RTXMacPackageStageStatus::SectionLookupFailed: return "section-lookup-failed";
    case RTXMacPackageStageStatus::DmaAllocationFailed: return "dma-allocation-failed";
    case RTXMacPackageStageStatus::DmaPopulationFailed: return "dma-population-failed";
    case RTXMacPackageStageStatus::PageAddressAllocationFailed:
      return "page-address-allocation-failed";
    case RTXMacPackageStageStatus::PageAddressValidationFailed:
      return "page-address-validation-failed";
    case RTXMacPackageStageStatus::DmaLayoutRejected:
      return "dma-layout-rejected";
  }
  return "unknown";
}
