#include "rtxmac/package_dma_resolve.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

using namespace rtxmac::nvidia::package;

namespace {

DmaStagingPlan MakePlan() {
  DmaStagingPlan plan{};
  plan.status = DmaStagingPlanStatus::Ok;
  const std::array<SectionKind, kSectionCount> kinds{
      SectionKind::GspFirmwareImage,
      SectionKind::GspFirmwareSignature,
      SectionKind::GspBootloader,
      SectionKind::FrtsFwsecImage,
      SectionKind::Sec2BooterImage,
  };
  for (std::size_t i = 0; i < kSectionCount; ++i) {
    auto& section = plan.sections[i];
    section.kind = kinds[i];
    section.layout = (i == 1 || i == 2) ? DmaSectionLayout::Linear
                                        : DmaSectionLayout::PageList;
    section.logicalBytes = 0x1800u + i;
    section.allocationBytes = 0x2000u;
    section.pageCount = 2u;
    plan.totalLogicalBytes += section.logicalBytes;
    plan.totalAllocationBytes += section.allocationBytes;
  }
  return plan;
}

std::array<std::vector<std::uint64_t>, kSectionCount> MakePages() {
  std::array<std::vector<std::uint64_t>, kSectionCount> pages{};
  for (std::size_t i = 0; i < kSectionCount; ++i) {
    const std::uint64_t base = 0x100000ull + static_cast<std::uint64_t>(i) * 0x10000ull;
    pages[i] = {base, base + 0x1000ull};
  }
  return pages;
}

std::array<StagedSectionPhysicalView, kSectionCount> MakeViews(
    const DmaStagingPlan& plan,
    const std::array<std::vector<std::uint64_t>, kSectionCount>& pages) {
  std::array<StagedSectionPhysicalView, kSectionCount> views{};
  for (std::size_t i = 0; i < kSectionCount; ++i) {
    views[i].kind = plan.sections[i].kind;
    views[i].layout = plan.sections[i].layout;
    views[i].logicalBytes = plan.sections[i].logicalBytes;
    views[i].allocationBytes = plan.sections[i].allocationBytes;
    views[i].pageAddresses = pages[i];
  }
  return views;
}

} // namespace

int main() {
  auto plan = MakePlan();
  auto pages = MakePages();
  auto views = MakeViews(plan, pages);

  const auto summary = ResolvePackageDmaSummary(plan, views);
  assert(summary.status == DmaResolveStatus::Ok);
  assert(summary.totalPages == 10u);
  assert(summary.allocations[0].kind == rtxmac::nvidia::gsp::AllocationKind::Radix3Firmware);
  assert(summary.allocations[1].kind == rtxmac::nvidia::gsp::AllocationKind::FirmwareSignature);
  assert(summary.allocations[2].kind == rtxmac::nvidia::gsp::AllocationKind::GspBootloader);
  assert(summary.allocations[1].baseAddress == pages[1][0]);
  assert(summary.allocations[1].pageCount == 2u);
  assert(summary.allocations[1].layout == rtxmac::nvidia::gsp::DmaLayoutRequirement::Linear);
  assert(summary.allocations[0].layout == rtxmac::nvidia::gsp::DmaLayoutRequirement::PageList);

  auto resolved = ResolvePackageDma(plan, views);
  assert(resolved.status == DmaResolveStatus::Ok);
  assert(resolved.totalPages == 10u);
  assert(resolved.allocations[0].kind == rtxmac::nvidia::gsp::AllocationKind::Radix3Firmware);
  assert(resolved.allocations[1].kind == rtxmac::nvidia::gsp::AllocationKind::FirmwareSignature);
  assert(resolved.allocations[2].kind == rtxmac::nvidia::gsp::AllocationKind::GspBootloader);
  assert(resolved.allocations[1].baseAddress == pages[1][0]);
  assert(resolved.allocations[1].pageAddresses.size() == 2u);

  {
    auto badPlan = plan;
    badPlan.status = DmaStagingPlanStatus::PackageNotVerified;
    assert(ResolvePackageDmaSummary(badPlan, views).status == DmaResolveStatus::BadPlan);
    assert(ResolvePackageDma(badPlan, views).status == DmaResolveStatus::BadPlan);
  }
  {
    auto bad = views;
    bad[0].logicalBytes++;
    assert(ResolvePackageDmaSummary(plan, bad).status == DmaResolveStatus::SectionMismatch);
    assert(ResolvePackageDma(plan, bad).status == DmaResolveStatus::SectionMismatch);
  }
  {
    auto badPages = pages;
    badPages[0][0] += 1u;
    auto bad = MakeViews(plan, badPages);
    assert(ResolvePackageDmaSummary(plan, bad).status == DmaResolveStatus::BadPageAddress);
    assert(ResolvePackageDma(plan, bad).status == DmaResolveStatus::BadPageAddress);
  }
  {
    auto badPages = pages;
    badPages[1][1] += 0x1000u;
    auto bad = MakeViews(plan, badPages);
    assert(ResolvePackageDmaSummary(plan, bad).status == DmaResolveStatus::LinearLayoutRejected);
    assert(ResolvePackageDma(plan, bad).status == DmaResolveStatus::LinearLayoutRejected);
  }
  {
    auto badPages = pages;
    badPages[4].pop_back();
    auto bad = MakeViews(plan, badPages);
    assert(ResolvePackageDmaSummary(plan, bad).status == DmaResolveStatus::BadPageCount);
    assert(ResolvePackageDma(plan, bad).status == DmaResolveStatus::BadPageCount);
  }

  return 0;
}
