#include "rtxmac/package_dma_resolve.hpp"

#include <limits>

namespace rtxmac::nvidia::package {
namespace {

using rtxmac::nvidia::gsp::AllocationKind;
using rtxmac::nvidia::gsp::DmaLayoutRequirement;
using rtxmac::nvidia::gsp::ResolvedDmaAllocation;

AllocationKind MapAllocationKind(SectionKind kind) noexcept {
  switch (kind) {
    case SectionKind::GspFirmwareImage: return AllocationKind::Radix3Firmware;
    case SectionKind::GspFirmwareSignature: return AllocationKind::FirmwareSignature;
    case SectionKind::GspBootloader: return AllocationKind::GspBootloader;
    case SectionKind::FrtsFwsecImage: return AllocationKind::FrtsFwsecImage;
    case SectionKind::Sec2BooterImage: return AllocationKind::Sec2BooterImage;
  }
  return AllocationKind::Radix3Firmware;
}

DmaLayoutRequirement MapLayout(DmaSectionLayout layout) noexcept {
  return layout == DmaSectionLayout::Linear
      ? DmaLayoutRequirement::Linear
      : DmaLayoutRequirement::PageList;
}

bool ValidPageList(std::span<const std::uint64_t> pages,
                   std::uint64_t pageBytes) noexcept {
  if (pages.empty() || pageBytes == 0u || (pageBytes & (pageBytes - 1u)) != 0u)
    return false;
  for (const std::uint64_t address : pages) {
    if ((address & (pageBytes - 1u)) != 0u) return false;
  }
  return true;
}

} // namespace

ResolvedPackageDma ResolvePackageDma(
    const DmaStagingPlan& plan,
    std::span<const StagedSectionPhysicalView> staged) noexcept {
  ResolvedPackageDma out{};
  if (plan.status != DmaStagingPlanStatus::Ok) {
    out.status = DmaResolveStatus::BadPlan;
    return out;
  }
  if (staged.size() != kSectionCount) {
    out.status = DmaResolveStatus::WrongSectionCount;
    return out;
  }

  for (std::size_t i = 0u; i < kSectionCount; ++i) {
    const DmaSectionPlan& expected = plan.sections[i];
    const StagedSectionPhysicalView& actual = staged[i];
    if (actual.kind != expected.kind || actual.layout != expected.layout ||
        actual.logicalBytes != expected.logicalBytes) {
      out.status = DmaResolveStatus::SectionMismatch;
      return out;
    }
    if (actual.allocationBytes != expected.allocationBytes ||
        actual.allocationBytes == 0u ||
        (actual.allocationBytes % kPackageDmaPageBytes) != 0u) {
      out.status = DmaResolveStatus::BadAllocationSize;
      return out;
    }
    if (expected.pageCount == 0u ||
        expected.pageCount > std::numeric_limits<std::size_t>::max() ||
        actual.pageAddresses.size() != static_cast<std::size_t>(expected.pageCount)) {
      out.status = DmaResolveStatus::BadPageCount;
      return out;
    }
    if (!ValidPageList(actual.pageAddresses, kPackageDmaPageBytes)) {
      out.status = DmaResolveStatus::BadPageAddress;
      return out;
    }
    if (expected.layout == DmaSectionLayout::Linear &&
        !IsLinearDmaPageList(actual.pageAddresses, kPackageDmaPageBytes)) {
      out.status = DmaResolveStatus::LinearLayoutRejected;
      return out;
    }
    if (out.totalPages > std::numeric_limits<std::uint64_t>::max() - expected.pageCount) {
      out.status = DmaResolveStatus::BadPageCount;
      return out;
    }

    ResolvedDmaAllocation& resolved = out.allocations[i];
    resolved.kind = MapAllocationKind(expected.kind);
    resolved.layout = MapLayout(expected.layout);
    resolved.baseAddress = actual.pageAddresses.front();
    resolved.allocationBytes = actual.allocationBytes;
    resolved.pageAddresses.assign(actual.pageAddresses.begin(), actual.pageAddresses.end());
    out.totalPages += expected.pageCount;
  }

  out.status = DmaResolveStatus::Ok;
  return out;
}

const char* DmaResolveStatusName(DmaResolveStatus status) noexcept {
  switch (status) {
    case DmaResolveStatus::Ok: return "ok";
    case DmaResolveStatus::BadPlan: return "bad-plan";
    case DmaResolveStatus::WrongSectionCount: return "wrong-section-count";
    case DmaResolveStatus::SectionMismatch: return "section-mismatch";
    case DmaResolveStatus::BadAllocationSize: return "bad-allocation-size";
    case DmaResolveStatus::BadPageCount: return "bad-page-count";
    case DmaResolveStatus::BadPageAddress: return "bad-page-address";
    case DmaResolveStatus::LinearLayoutRejected: return "linear-layout-rejected";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::package
