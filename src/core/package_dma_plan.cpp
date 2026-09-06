#include "rtxmac/package_dma_plan.hpp"

#include <limits>

namespace rtxmac::nvidia::package {
namespace {

bool Add(std::uint64_t a, std::uint64_t b, std::uint64_t* out) noexcept {
  if (!out || a > std::numeric_limits<std::uint64_t>::max() - b) return false;
  *out = a + b;
  return true;
}

bool AlignUp(std::uint64_t value,
             std::uint64_t alignment,
             std::uint64_t* out) noexcept {
  if (!out || alignment == 0u || (alignment & (alignment - 1u)) != 0u) return false;
  const std::uint64_t mask = alignment - 1u;
  if (value > std::numeric_limits<std::uint64_t>::max() - mask) return false;
  *out = (value + mask) & ~mask;
  return true;
}

DmaSectionLayout RequiredLayout(SectionKind kind) noexcept {
  switch (kind) {
    case SectionKind::GspFirmwareSignature:
    case SectionKind::GspBootloader:
      return DmaSectionLayout::Linear;
    case SectionKind::GspFirmwareImage:
    case SectionKind::FrtsFwsecImage:
    case SectionKind::Sec2BooterImage:
      return DmaSectionLayout::PageList;
  }
  return DmaSectionLayout::PageList;
}

} // namespace

DmaStagingPlan PlanPackageDmaStaging(const PackageView& view) noexcept {
  DmaStagingPlan out{};
  if (view.status != ParseStatus::Ok) {
    out.status = DmaStagingPlanStatus::PackageNotVerified;
    return out;
  }

  std::uint64_t logicalTotal = 0u;
  std::uint64_t allocationTotal = 0u;
  for (std::size_t i = 0u; i < kSectionCount; ++i) {
    const SectionRecord& section = view.sections[i];
    if (section.size == 0u) {
      out.status = DmaStagingPlanStatus::MissingOrEmptySection;
      return out;
    }

    std::uint64_t allocationBytes = 0u;
    if (!AlignUp(section.size, kPackageDmaPageBytes, &allocationBytes) ||
        allocationBytes == 0u) {
      out.status = DmaStagingPlanStatus::SizeOverflow;
      return out;
    }

    if (!Add(logicalTotal, section.size, &logicalTotal) ||
        !Add(allocationTotal, allocationBytes, &allocationTotal)) {
      out.status = DmaStagingPlanStatus::TotalOverflow;
      return out;
    }

    out.sections[i] = {
        .kind = section.kind,
        .layout = RequiredLayout(section.kind),
        .logicalBytes = section.size,
        .allocationBytes = allocationBytes,
        .pageCount = allocationBytes / kPackageDmaPageBytes,
    };
  }

  out.totalLogicalBytes = logicalTotal;
  out.totalAllocationBytes = allocationTotal;
  out.status = DmaStagingPlanStatus::Ok;
  return out;
}

const char* DmaStagingPlanStatusName(DmaStagingPlanStatus status) noexcept {
  switch (status) {
    case DmaStagingPlanStatus::Ok: return "ok";
    case DmaStagingPlanStatus::PackageNotVerified: return "package-not-verified";
    case DmaStagingPlanStatus::MissingOrEmptySection: return "missing-or-empty-section";
    case DmaStagingPlanStatus::SizeOverflow: return "size-overflow";
    case DmaStagingPlanStatus::TotalOverflow: return "total-overflow";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::package
