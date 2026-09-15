#include "rtxmac/ga10x_boot_resolve.hpp"

#include <limits>

namespace rtxmac::nvidia::prototype {
namespace {

using gsp::AllocationKind;
using gsp::DmaLayoutRequirement;

const package::ResolvedPackageDmaEntry* FindPackageEntry(
    const package::ResolvedPackageDmaSummary& summary,
    AllocationKind kind) noexcept {
  for (const auto& entry : summary.allocations) {
    if (entry.kind == kind) return &entry;
  }
  return nullptr;
}

bool ValidPages(std::span<const std::uint64_t> pages) noexcept {
  if (pages.empty()) return false;
  for (const std::uint64_t address : pages) {
    if (address == 0u || (address % kBootPreparePageBytes) != 0u) return false;
  }
  return true;
}

} // namespace

ResolvedBootMemory ResolveGa10xBootMemory(
    const Profile& profile,
    const BootPreparationPlan& plan,
    const package::ResolvedPackageDmaSummary& stagedPackage,
    std::span<const GeneratedPhysicalView> generated) noexcept {
  ResolvedBootMemory out{};
  if (profile.status != ProfileStatus::Ok || !profile.manifest.valid) {
    out.status = BootResolveStatus::BadProfile;
    return out;
  }
  if (plan.status != BootPreparationPlanStatus::Ok) {
    out.status = BootResolveStatus::BadPreparationPlan;
    return out;
  }
  if (stagedPackage.status != package::DmaResolveStatus::Ok) {
    out.status = BootResolveStatus::BadStagedPackageSummary;
    return out;
  }
  if (generated.size() != plan.generated.size()) {
    out.status = BootResolveStatus::WrongGeneratedCount;
    return out;
  }

  std::array<std::uint64_t, kLibosLogRegionCount> logBases{};
  std::size_t logIndex = 0u;

  for (std::size_t i = 0u; i < plan.generated.size(); ++i) {
    const GeneratedDmaRequirement& requirement = plan.generated[i];
    const GeneratedPhysicalView& actual = generated[i];
    if (actual.kind != requirement.kind) {
      out.status = BootResolveStatus::GeneratedKindMismatch;
      return out;
    }
    if (requirement.pageCount == 0u ||
        requirement.pageCount > std::numeric_limits<std::size_t>::max() ||
        actual.pageAddresses.size() != static_cast<std::size_t>(requirement.pageCount)) {
      out.status = BootResolveStatus::GeneratedPageCountMismatch;
      return out;
    }
    if (!ValidPages(actual.pageAddresses)) {
      out.status = BootResolveStatus::GeneratedPageAddressInvalid;
      return out;
    }
    if (requirement.layout == GeneratedDmaLayout::Linear &&
        !package::IsLinearDmaPageList(actual.pageAddresses,
                                     kBootPreparePageBytes)) {
      out.status = BootResolveStatus::GeneratedLinearLayoutRejected;
      return out;
    }
    if (out.generatedTotalPages >
        std::numeric_limits<std::uint64_t>::max() - requirement.pageCount) {
      out.status = BootResolveStatus::GeneratedPageCountMismatch;
      return out;
    }
    out.generatedTotalPages += requirement.pageCount;

    const std::uint64_t base = actual.pageAddresses.front();
    switch (requirement.kind) {
      case GeneratedBufferKind::QueueBacking:
        out.addresses.queueBacking = base;
        break;
      case GeneratedBufferKind::CachedArguments:
        out.addresses.cachedArguments = base;
        break;
      case GeneratedBufferKind::LibosInitArguments:
        out.addresses.libosInitArguments = base;
        break;
      case GeneratedBufferKind::WprMetadata:
        out.addresses.wprMetadata = base;
        break;
      case GeneratedBufferKind::Radix3Firmware:
        out.addresses.radix3FirmwareRoot = base;
        break;
      case GeneratedBufferKind::LogInit:
      case GeneratedBufferKind::LogIntr:
      case GeneratedBufferKind::LogRm:
      case GeneratedBufferKind::LogMnoc:
      case GeneratedBufferKind::LogKrnl:
        if (logIndex >= logBases.size()) {
          out.status = BootResolveStatus::GeneratedKindMismatch;
          return out;
        }
        logBases[logIndex++] = base;
        break;
    }
  }
  if (logIndex != logBases.size()) {
    out.status = BootResolveStatus::GeneratedKindMismatch;
    return out;
  }

  const auto* signature =
      FindPackageEntry(stagedPackage, AllocationKind::FirmwareSignature);
  const auto* bootloader =
      FindPackageEntry(stagedPackage, AllocationKind::GspBootloader);
  if (!signature || !bootloader) {
    out.status = BootResolveStatus::PackageReuseMissing;
    return out;
  }
  if (signature->layout != DmaLayoutRequirement::Linear ||
      bootloader->layout != DmaLayoutRequirement::Linear ||
      signature->baseAddress == 0u || bootloader->baseAddress == 0u ||
      (signature->baseAddress % kBootPreparePageBytes) != 0u ||
      (bootloader->baseAddress % kBootPreparePageBytes) != 0u ||
      signature->allocationBytes != plan.reusedSignatureAllocationBytes ||
      bootloader->allocationBytes != plan.reusedBootloaderAllocationBytes) {
    out.status = BootResolveStatus::PackageReuseMismatch;
    return out;
  }

  out.addresses.firmwareSignature = signature->baseAddress;
  out.addresses.gspBootloader = bootloader->baseAddress;
  out.addresses.frtsFwsecImage = plan.framebuffer.frtsFwsecOffset;
  out.addresses.sec2BooterImage = plan.framebuffer.sec2BooterOffset;

  out.libosRegions = {{
      {"LOGINIT", logBases[0], kLibosLogRegionBytes},
      {"LOGINTR", logBases[1], kLibosLogRegionBytes},
      {"LOGRM", logBases[2], kLibosLogRegionBytes},
      {"LOGMNOC", logBases[3], kLibosLogRegionBytes},
      {"LOGKRNL", logBases[4], kLibosLogRegionBytes},
      {"RMARGS", out.addresses.cachedArguments, kBootPreparePageBytes},
  }};

  out.status = BootResolveStatus::Ok;
  return out;
}

const char* BootResolveStatusName(BootResolveStatus status) noexcept {
  switch (status) {
    case BootResolveStatus::Ok: return "ok";
    case BootResolveStatus::BadProfile: return "bad-profile";
    case BootResolveStatus::BadPreparationPlan: return "bad-preparation-plan";
    case BootResolveStatus::BadStagedPackageSummary:
      return "bad-staged-package-summary";
    case BootResolveStatus::WrongGeneratedCount: return "wrong-generated-count";
    case BootResolveStatus::GeneratedKindMismatch: return "generated-kind-mismatch";
    case BootResolveStatus::GeneratedPageCountMismatch:
      return "generated-page-count-mismatch";
    case BootResolveStatus::GeneratedPageAddressInvalid:
      return "generated-page-address-invalid";
    case BootResolveStatus::GeneratedLinearLayoutRejected:
      return "generated-linear-layout-rejected";
    case BootResolveStatus::PackageReuseMissing: return "package-reuse-missing";
    case BootResolveStatus::PackageReuseMismatch: return "package-reuse-mismatch";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::prototype
