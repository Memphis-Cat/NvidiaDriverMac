#include "rtxmac/package_dma_plan.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

using namespace rtxmac::nvidia::package;

namespace {

PackageView MakeView() {
  PackageView view{};
  view.status = ParseStatus::Ok;
  view.packageBytes = 0x20000u;
  view.sections = {{
      {SectionKind::GspFirmwareImage, 0x1000u, 1u, {}},
      {SectionKind::GspFirmwareSignature, 0x2000u, 0x1000u, {}},
      {SectionKind::GspBootloader, 0x3000u, 0x1001u, {}},
      {SectionKind::FrtsFwsecImage, 0x5000u, 0x1fffu, {}},
      {SectionKind::Sec2BooterImage, 0x7000u, 0x2000u, {}},
  }};
  return view;
}

} // namespace

int main() {
  {
    const auto plan = PlanPackageDmaStaging(MakeView());
    assert(plan.status == DmaStagingPlanStatus::Ok);
    assert(plan.sections[0].layout == DmaSectionLayout::PageList);
    assert(plan.sections[1].layout == DmaSectionLayout::Linear);
    assert(plan.sections[2].layout == DmaSectionLayout::Linear);
    assert(plan.sections[3].layout == DmaSectionLayout::PageList);
    assert(plan.sections[4].layout == DmaSectionLayout::PageList);
    assert(plan.sections[0].logicalBytes == 1u);
    assert(plan.sections[0].allocationBytes == 0x1000u);
    assert(plan.sections[0].pageCount == 1u);
    assert(plan.sections[1].allocationBytes == 0x1000u);
    assert(plan.sections[2].allocationBytes == 0x2000u);
    assert(plan.sections[2].pageCount == 2u);
    assert(plan.sections[3].allocationBytes == 0x2000u);
    assert(plan.sections[4].allocationBytes == 0x2000u);
    assert(plan.totalLogicalBytes == 1u + 0x1000u + 0x1001u + 0x1fffu + 0x2000u);
    assert(plan.totalAllocationBytes == 0x8000u);
  }

  {
    const std::array<std::uint64_t, 1> onePage{0x10000000ull};
    assert(IsLinearDmaPageList(onePage));

    const std::array<std::uint64_t, 4> contiguous{
        0x20000000ull, 0x20001000ull, 0x20002000ull, 0x20003000ull};
    assert(IsLinearDmaPageList(contiguous));

    const std::array<std::uint64_t, 3> fragmented{
        0x30000000ull, 0x30001000ull, 0x31000000ull};
    assert(!IsLinearDmaPageList(fragmented));

    const std::array<std::uint64_t, 2> unaligned{
        0x40000000ull, 0x40001001ull};
    assert(!IsLinearDmaPageList(unaligned));

    constexpr std::uint64_t kHighestAligned =
        std::numeric_limits<std::uint64_t>::max() - 0xfffull;
    const std::array<std::uint64_t, 2> wrapped{kHighestAligned, 0u};
    assert(!IsLinearDmaPageList(wrapped));

    assert(!IsLinearDmaPageList({}));
    assert(!IsLinearDmaPageList(onePage, 3000u));
  }

  {
    auto view = MakeView();
    view.status = ParseStatus::HashMismatch;
    assert(PlanPackageDmaStaging(view).status ==
           DmaStagingPlanStatus::PackageNotVerified);
  }

  {
    auto view = MakeView();
    view.sections[2].size = 0u;
    assert(PlanPackageDmaStaging(view).status ==
           DmaStagingPlanStatus::MissingOrEmptySection);
  }

  {
    auto view = MakeView();
    view.sections[0].size = std::numeric_limits<std::uint64_t>::max();
    assert(PlanPackageDmaStaging(view).status ==
           DmaStagingPlanStatus::SizeOverflow);
  }

  {
    auto view = MakeView();
    constexpr std::uint64_t kLargestAligned =
        std::numeric_limits<std::uint64_t>::max() - 0xfffull;
    view.sections[0].size = kLargestAligned;
    view.sections[1].size = kLargestAligned;
    assert(PlanPackageDmaStaging(view).status ==
           DmaStagingPlanStatus::TotalOverflow);
  }

  return 0;
}
