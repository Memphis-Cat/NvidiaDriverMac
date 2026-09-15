#include "rtxmac/ga10x_boot_resolve.hpp"

#include <array>
#include <cassert>
#include <iostream>
#include <vector>

namespace {

rtxmac::nvidia::package::PackageView MakePackage() {
  using namespace rtxmac::nvidia::package;
  PackageView view{};
  view.status = ParseStatus::Ok;
  view.packageBytes = 0x4000000ull;
  view.metadata.pci = {0x10DEu, 0x2489u, 0x10DEu, 0x1538u};
  view.metadata.vramBytes = 0x200000000ull;
  view.metadata.gspAppVersion = 0x12345678u;
  view.metadata.gspMonitorCodeOffset = 0x1000u;
  view.metadata.gspMonitorDataOffset = 0x9000u;
  view.metadata.gspManifestOffset = 0x200u;
  view.metadata.fwsecPkcDataOffset = 0x100u;
  view.metadata.fwsecImemPhysBase = 0x200u;
  view.metadata.fwsecImemLoadSize = 0x1000u;
  view.metadata.fwsecImemVirtBase = 0x400u;
  view.metadata.fwsecDmemPhysBase = 0x600u;
  view.metadata.fwsecDmemLoadSize = 0x1000u;
  view.metadata.fwsecEngineIdMask = 1u;
  view.metadata.fwsecUcodeId = 5u;
  view.metadata.sec2CodeOffset = 0x1000u;
  view.metadata.sec2CodeSize = 0x2000u;
  view.metadata.sec2DataOffset = 0x4000u;
  view.metadata.sec2DataSize = 0x1000u;
  view.sections = {{
      {SectionKind::GspFirmwareImage, 0x1000u, 0x200FEFull, {}},
      {SectionKind::GspFirmwareSignature, 0x202000u, 0x1000ull, {}},
      {SectionKind::GspBootloader, 0x203000u, 0x18000ull, {}},
      {SectionKind::FrtsFwsecImage, 0x21B000u, 0x20000ull, {}},
      {SectionKind::Sec2BooterImage, 0x23B000u, 0x10000ull, {}},
  }};
  return view;
}

std::vector<std::uint64_t> MakePages(std::uint64_t base,
                                     std::uint64_t count,
                                     bool fragmented = false) {
  std::vector<std::uint64_t> pages;
  pages.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t i = 0u; i < count; ++i) {
    const std::uint64_t gap = fragmented && i >= count / 2u ? 0x1000000ull : 0u;
    pages.push_back(base + i * 0x1000ull + gap);
  }
  return pages;
}

} // namespace

int main() {
  using namespace rtxmac::nvidia;
  using namespace rtxmac::nvidia::prototype;

  const auto profile = BuildGa10xPrototypeProfile(MakePackage());
  const auto plan = PlanGa10xBootPreparation(profile);
  assert(profile.status == ProfileStatus::Ok);
  assert(plan.status == BootPreparationPlanStatus::Ok);

  std::array<std::vector<std::uint64_t>, kGeneratedDmaRequirementCount> pages{};
  std::array<GeneratedPhysicalView, kGeneratedDmaRequirementCount> views{};
  std::uint64_t nextBase = 0x10000000ull;
  for (std::size_t i = 0u; i < plan.generated.size(); ++i) {
    const auto& requirement = plan.generated[i];
    const bool fragmented = requirement.layout == GeneratedDmaLayout::PageList;
    pages[i] = MakePages(nextBase, requirement.pageCount, fragmented);
    views[i] = {requirement.kind, pages[i]};
    nextBase += 0x02000000ull;
  }

  package::ResolvedPackageDmaSummary staged{};
  staged.status = package::DmaResolveStatus::Ok;
  staged.allocations[0] = {
      gsp::AllocationKind::Radix3Firmware,
      gsp::DmaLayoutRequirement::PageList,
      0x70000000ull,
      0x201000ull,
      513u};
  staged.allocations[1] = {
      gsp::AllocationKind::FirmwareSignature,
      gsp::DmaLayoutRequirement::Linear,
      0x71000000ull,
      plan.reusedSignatureAllocationBytes,
      plan.reusedSignatureAllocationBytes / 0x1000ull};
  staged.allocations[2] = {
      gsp::AllocationKind::GspBootloader,
      gsp::DmaLayoutRequirement::Linear,
      0x72000000ull,
      plan.reusedBootloaderAllocationBytes,
      plan.reusedBootloaderAllocationBytes / 0x1000ull};
  staged.allocations[3] = {
      gsp::AllocationKind::FrtsFwsecImage,
      gsp::DmaLayoutRequirement::PageList,
      0x73000000ull,
      0x20000ull,
      32u};
  staged.allocations[4] = {
      gsp::AllocationKind::Sec2BooterImage,
      gsp::DmaLayoutRequirement::PageList,
      0x74000000ull,
      0x10000ull,
      16u};

  const auto resolved = ResolveGa10xBootMemory(profile, plan, staged, views);
  assert(resolved.status == BootResolveStatus::Ok);
  assert(resolved.addresses.queueBacking == pages[0][0]);
  assert(resolved.addresses.cachedArguments == pages[1][0]);
  assert(resolved.addresses.libosInitArguments == pages[2][0]);
  assert(resolved.addresses.wprMetadata == pages[3][0]);
  assert(resolved.addresses.radix3FirmwareRoot == pages[4][0]);
  assert(resolved.addresses.firmwareSignature == 0x71000000ull);
  assert(resolved.addresses.gspBootloader == 0x72000000ull);
  assert(resolved.addresses.frtsFwsecImage == plan.framebuffer.frtsFwsecOffset);
  assert(resolved.addresses.sec2BooterImage == plan.framebuffer.sec2BooterOffset);

  assert(resolved.libosRegions[0].id == "LOGINIT");
  assert(resolved.libosRegions[1].id == "LOGINTR");
  assert(resolved.libosRegions[2].id == "LOGRM");
  assert(resolved.libosRegions[3].id == "LOGMNOC");
  assert(resolved.libosRegions[4].id == "LOGKRNL");
  assert(resolved.libosRegions[5].id == "RMARGS");
  for (std::size_t i = 0u; i < 5u; ++i) {
    assert(resolved.libosRegions[i].physicalAddress == pages[5u + i][0]);
    assert(resolved.libosRegions[i].size == kLibosLogRegionBytes);
  }
  assert(resolved.libosRegions[5].physicalAddress == pages[1][0]);
  assert(resolved.libosRegions[5].size == 0x1000u);

  std::uint64_t expectedPages = 0u;
  for (const auto& requirement : plan.generated) expectedPages += requirement.pageCount;
  assert(resolved.generatedTotalPages == expectedPages);

  auto fragmentedLinearPages = pages;
  fragmentedLinearPages[5][8] += 0x100000ull;
  auto fragmentedLinearViews = views;
  fragmentedLinearViews[5].pageAddresses = fragmentedLinearPages[5];
  assert(ResolveGa10xBootMemory(profile, plan, staged, fragmentedLinearViews).status ==
         BootResolveStatus::GeneratedLinearLayoutRejected);

  auto badStaged = staged;
  badStaged.allocations[2].allocationBytes += 0x1000u;
  assert(ResolveGa10xBootMemory(profile, plan, badStaged, views).status ==
         BootResolveStatus::PackageReuseMismatch);

  auto missingPackage = staged;
  missingPackage.status = package::DmaResolveStatus::BadPlan;
  assert(ResolveGa10xBootMemory(profile, plan, missingPackage, views).status ==
         BootResolveStatus::BadStagedPackageSummary);

  assert(ResolveGa10xBootMemory(
             profile, plan, staged,
             std::span<const GeneratedPhysicalView>(views.data(), views.size() - 1u)).status ==
         BootResolveStatus::WrongGeneratedCount);

  std::cout << "rtxmac GA10x cold boot address resolver tests passed\n";
}
