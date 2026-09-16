#include "RTXMacBootSession.hpp"

#include "RTXMacSystemInfo.hpp"
#include "rtxmac/sha256.hpp"

#include <limits>
#include <new>

namespace {

using rtxmac::nvidia::prototype::GeneratedDmaLayout;
constexpr std::uint32_t kGa104ChipId = 0x174u;

void ReleaseGeneratedBuffers(RTXMacColdBootSession* session) noexcept {
  if (!session) return;
  for (auto& generated : session->generated) {
    delete[] generated.pageAddresses;
    generated.pageAddresses = nullptr;
    generated.pageCount = 0u;
    RTXMacReleasePreparedDmaBuffer(&generated.dma);
  }
}

kern_return_t Fail(RTXMacColdBootSession* session,
                   RTXMacBootSessionStatus status,
                   kern_return_t ioStatus,
                   std::uint32_t failedIndex = 0xFFFFFFFFu) noexcept {
  if (session) {
    ReleaseGeneratedBuffers(session);
    session->ready = false;
    session->status = status;
    session->ioStatus = ioStatus;
    session->failedGeneratedIndex = failedIndex;
    session->addresses = {};
    session->bootPhaseCount = 0u;
    session->executableWithCurrentCore = false;
  }
  return ioStatus;
}

kern_return_t PopulateGenerated(
    RTXMacGeneratedDmaBuffer& generated,
    std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.empty() || bytes.size() != generated.logicalBytes) {
    return kIOReturnBadArgument;
  }
  return RTXMacCopyIntoPreparedDmaBufferPadded(
      &generated.dma, bytes.data(), bytes.size());
}

} // namespace

kern_return_t RTXMacPrepareColdBootSession(
    IOPCIDevice* pci,
    std::span<const std::uint8_t> bytes,
    const rtxmac::nvidia::package::PackageView& view,
    const RTXMacStagedPackage& staged,
    const RTXMacLiveBoundaryPreflight& liveBoundary,
    RTXMacColdBootSession* out) noexcept {
  using namespace rtxmac::nvidia;

  if (!pci || !out || bytes.empty() ||
      view.status != package::ParseStatus::Ok) {
    if (out) {
      RTXMacReleaseColdBootSession(out);
      return Fail(out, RTXMacBootSessionStatus::BadArgument,
                  kIOReturnBadArgument);
    }
    return kIOReturnBadArgument;
  }

  RTXMacReleaseColdBootSession(out);
  if (!staged.ready || staged.status != RTXMacPackageStageStatus::Ok) {
    return Fail(out, RTXMacBootSessionStatus::PackageNotReady,
                kIOReturnNotReady);
  }
  if (!rtxmac::Sha256Equal(staged.packageDigest, rtxmac::Sha256(bytes))) {
    return Fail(out, RTXMacBootSessionStatus::PackageMismatch,
                kIOReturnBadArgument);
  }

  out->boundaryStatus = liveBoundary.decision.status;
  out->activeMmuLock = liveBoundary.decision.activeMmuLock;
  out->prototypeBoundary = liveBoundary.decision.prototypeBoundary;
  out->effectiveBoundary = liveBoundary.decision.effectiveBoundary;
  const std::uint64_t expectedPrototypeBoundary =
      view.metadata.vramBytes > prototype::kVgaWorkspaceBytes
          ? view.metadata.vramBytes - prototype::kVgaWorkspaceBytes
          : 0u;
  const bool boundaryDecisionUsable =
      liveBoundary.captured && liveBoundary.ioStatus == kIOReturnSuccess &&
      (liveBoundary.decision.status ==
           prototype::ReservedBoundaryStatus::Ok ||
       liveBoundary.decision.status ==
           prototype::ReservedBoundaryStatus::RebuildRequired) &&
      liveBoundary.decision.prototypeBoundary == expectedPrototypeBoundary &&
      liveBoundary.decision.effectiveBoundary != 0u &&
      liveBoundary.decision.effectiveBoundary <= expectedPrototypeBoundary;
  if (!boundaryDecisionUsable) {
    return Fail(out, RTXMacBootSessionStatus::BoundaryRejected,
                liveBoundary.ioStatus == kIOReturnSuccess
                    ? kIOReturnBadArgument
                    : liveBoundary.ioStatus);
  }
  out->boundaryRebuilt =
      liveBoundary.decision.effectiveBoundary != expectedPrototypeBoundary;

  const prototype::Profile profile =
      prototype::BuildGa10xPrototypeProfile(
          view, liveBoundary.decision.effectiveBoundary);
  out->profileStatus = profile.status;
  if (profile.status != prototype::ProfileStatus::Ok) {
    return Fail(out, RTXMacBootSessionStatus::ProfileRejected,
                kIOReturnBadArgument);
  }

  const prototype::BootPreparationPlan plan =
      prototype::PlanGa10xBootPreparation(profile);
  out->planStatus = plan.status;
  out->totalAllocationBytes = plan.totalGeneratedAllocationBytes;
  if (plan.status != prototype::BootPreparationPlanStatus::Ok) {
    return Fail(out, RTXMacBootSessionStatus::PlanRejected,
                kIOReturnBadArgument);
  }

  const package::ResolvedPackageDmaSummary packageSummary =
      RTXMacDescribeStagedPackageDma(view, staged);
  out->packageResolveStatus = packageSummary.status;
  if (packageSummary.status != package::DmaResolveStatus::Ok) {
    return Fail(out, RTXMacBootSessionStatus::PackageSummaryRejected,
                kIOReturnBadArgument);
  }

  std::array<prototype::GeneratedPhysicalView,
             prototype::kGeneratedDmaRequirementCount>
      physicalViews{};

  for (std::size_t i = 0u; i < plan.generated.size(); ++i) {
    const prototype::GeneratedDmaRequirement& requirement = plan.generated[i];
    RTXMacGeneratedDmaBuffer& generated = out->generated[i];
    generated.kind = requirement.kind;
    generated.layout = requirement.layout;
    generated.logicalBytes = requirement.logicalBytes;
    generated.allocationBytes = requirement.allocationBytes;

    if (out->totalLogicalBytes >
        std::numeric_limits<std::uint64_t>::max() - requirement.logicalBytes) {
      return Fail(out, RTXMacBootSessionStatus::PlanRejected,
                  kIOReturnNoResources, static_cast<std::uint32_t>(i));
    }
    out->totalLogicalBytes += requirement.logicalBytes;

    kern_return_t kr = RTXMacAllocateAndPrepareDmaBuffer(
        pci, requirement.allocationBytes, &generated.dma);
    if (kr != kIOReturnSuccess) {
      return Fail(out, RTXMacBootSessionStatus::GeneratedAllocationFailed,
                  kr, static_cast<std::uint32_t>(i));
    }

    if (requirement.pageCount == 0u ||
        requirement.pageCount > std::numeric_limits<std::uint32_t>::max()) {
      return Fail(out, RTXMacBootSessionStatus::PageAddressAllocationFailed,
                  kIOReturnNoResources, static_cast<std::uint32_t>(i));
    }
    const auto expectedPages =
        static_cast<std::uint32_t>(requirement.pageCount);
    generated.pageAddresses =
        new (std::nothrow) std::uint64_t[expectedPages]();
    if (!generated.pageAddresses) {
      return Fail(out, RTXMacBootSessionStatus::PageAddressAllocationFailed,
                  kIOReturnNoMemory, static_cast<std::uint32_t>(i));
    }

    std::uint32_t actualPages = 0u;
    kr = RTXMacCollectDmaPageAddresses(
        &generated.dma, generated.pageAddresses, expectedPages, &actualPages);
    if (kr != kIOReturnSuccess || actualPages != expectedPages) {
      return Fail(out, RTXMacBootSessionStatus::PageAddressValidationFailed,
                  kr == kIOReturnSuccess ? kIOReturnError : kr,
                  static_cast<std::uint32_t>(i));
    }
    generated.pageCount = actualPages;

    const std::span<const std::uint64_t> pages(
        generated.pageAddresses, generated.pageCount);
    if (requirement.layout == GeneratedDmaLayout::Linear &&
        !package::IsLinearDmaPageList(pages, kRTXMacDmaPageBytes)) {
      return Fail(out, RTXMacBootSessionStatus::GeneratedLayoutRejected,
                  kIOReturnNoResources, static_cast<std::uint32_t>(i));
    }

    physicalViews[i] = {
        .kind = requirement.kind,
        .pageAddresses = pages,
    };
  }

  const prototype::ResolvedBootMemory resolved =
      prototype::ResolveGa10xBootMemory(
          profile, plan, packageSummary, physicalViews);
  out->bootResolveStatus = resolved.status;
  if (resolved.status != prototype::BootResolveStatus::Ok) {
    return Fail(out, RTXMacBootSessionStatus::AddressResolveFailed,
                kIOReturnError);
  }
  out->totalPages = resolved.generatedTotalPages;
  out->addresses = resolved.addresses;

  RTXMacSystemInfoSnapshot systemInfo{};
  kern_return_t kr = RTXMacCollectSystemInfo(pci, &systemInfo);
  if (kr != kIOReturnSuccess) {
    return Fail(out, RTXMacBootSessionStatus::SystemInfoFailed, kr);
  }

  const std::span<const std::uint8_t> firmware = package::FindSection(
      bytes, view, package::SectionKind::GspFirmwareImage);
  const auto registry = gsp::DefaultBootstrapRegistry();
  auto artifacts = gsp::BuildResolvedArtifacts(
      profile.manifest,
      resolved.addresses,
      profile.gspBootloader,
      physicalViews[0].pageAddresses,
      physicalViews[4].pageAddresses,
      firmware,
      resolved.libosRegions,
      systemInfo.inputs,
      registry);
  if (!artifacts) {
    return Fail(out, RTXMacBootSessionStatus::ArtifactBuildFailed,
                kIOReturnError);
  }

  const std::array<std::span<const std::uint8_t>, 5u> artifactBytes{{
      std::span<const std::uint8_t>(
          artifacts->sharedQueueAllocation.data(),
          artifacts->sharedQueueAllocation.size()),
      std::span<const std::uint8_t>(
          artifacts->cachedArguments.data(), artifacts->cachedArguments.size()),
      std::span<const std::uint8_t>(
          artifacts->libosInitArguments.data(),
          artifacts->libosInitArguments.size()),
      std::span<const std::uint8_t>(
          artifacts->wprMetadata.data(), artifacts->wprMetadata.size()),
      std::span<const std::uint8_t>(
          artifacts->radix3FirmwareAllocation.data(),
          artifacts->radix3FirmwareAllocation.size()),
  }};
  for (std::size_t i = 0u; i < artifactBytes.size(); ++i) {
    kr = PopulateGenerated(out->generated[i], artifactBytes[i]);
    if (kr != kIOReturnSuccess) {
      return Fail(out, RTXMacBootSessionStatus::ArtifactPopulationFailed,
                  kr, static_cast<std::uint32_t>(i));
    }
  }
  for (std::size_t i = artifactBytes.size(); i < out->generated.size(); ++i) {
    kr = RTXMacZeroPreparedDmaBuffer(&out->generated[i].dma);
    if (kr != kIOReturnSuccess) {
      return Fail(out, RTXMacBootSessionStatus::ArtifactPopulationFailed,
                  kr, static_cast<std::uint32_t>(i));
    }
  }

  const gsp::BootSequence sequence = gsp::PlanBootSequence(
      profile.manifest,
      resolved.addresses,
      profile.gspBootloader,
      profile.fwsec,
      profile.sec2Booter,
      kGa104ChipId);
  if (!sequence.valid || sequence.phases.empty() ||
      sequence.phases.size() > std::numeric_limits<std::uint32_t>::max()) {
    return Fail(out, RTXMacBootSessionStatus::SequenceRejected,
                kIOReturnError);
  }

  out->bootPhaseCount = static_cast<std::uint32_t>(sequence.phases.size());
  out->executableWithCurrentCore = sequence.executableWithCurrentCore;
  out->ready = true;
  out->status = RTXMacBootSessionStatus::Ok;
  out->ioStatus = kIOReturnSuccess;
  out->failedGeneratedIndex = 0xFFFFFFFFu;
  return kIOReturnSuccess;
}

void RTXMacReleaseColdBootSession(RTXMacColdBootSession* session) noexcept {
  if (!session) return;
  ReleaseGeneratedBuffers(session);
  *session = {};
}

const char* RTXMacBootSessionStatusName(
    RTXMacBootSessionStatus status) noexcept {
  switch (status) {
    case RTXMacBootSessionStatus::Idle: return "idle";
    case RTXMacBootSessionStatus::Ok: return "ok";
    case RTXMacBootSessionStatus::BadArgument: return "bad-argument";
    case RTXMacBootSessionStatus::PackageNotReady: return "package-not-ready";
    case RTXMacBootSessionStatus::PackageMismatch: return "package-mismatch";
    case RTXMacBootSessionStatus::ProfileRejected: return "profile-rejected";
    case RTXMacBootSessionStatus::PlanRejected: return "plan-rejected";
    case RTXMacBootSessionStatus::PackageSummaryRejected:
      return "package-summary-rejected";
    case RTXMacBootSessionStatus::GeneratedAllocationFailed:
      return "generated-allocation-failed";
    case RTXMacBootSessionStatus::PageAddressAllocationFailed:
      return "page-address-allocation-failed";
    case RTXMacBootSessionStatus::PageAddressValidationFailed:
      return "page-address-validation-failed";
    case RTXMacBootSessionStatus::GeneratedLayoutRejected:
      return "generated-layout-rejected";
    case RTXMacBootSessionStatus::AddressResolveFailed:
      return "address-resolve-failed";
    case RTXMacBootSessionStatus::SystemInfoFailed:
      return "system-info-failed";
    case RTXMacBootSessionStatus::ArtifactBuildFailed:
      return "artifact-build-failed";
    case RTXMacBootSessionStatus::ArtifactPopulationFailed:
      return "artifact-population-failed";
    case RTXMacBootSessionStatus::SequenceRejected:
      return "sequence-rejected";
    case RTXMacBootSessionStatus::BoundaryRejected:
      return "boundary-rejected";
  }
  return "unknown";
}
