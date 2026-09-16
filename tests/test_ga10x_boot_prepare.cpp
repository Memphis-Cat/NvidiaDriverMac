#include "rtxmac/ga10x_boot_prepare.hpp"

#include <cassert>
#include <iostream>

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
      {SectionKind::FrtsFwsecImage, 0x21B000u, 0x20003ull, {}},
      {SectionKind::Sec2BooterImage, 0x23C000u, 0x10005ull, {}},
  }};
  return view;
}

} // namespace

int main() {
  using namespace rtxmac::nvidia::prototype;

  const auto profile = BuildGa10xPrototypeProfile(MakePackage());
  assert(profile.status == ProfileStatus::Ok);

  const auto plan = PlanGa10xBootPreparation(profile);
  assert(plan.status == BootPreparationPlanStatus::Ok);
  assert(plan.generated.size() == kGeneratedDmaRequirementCount);

  assert(plan.generated[0].kind == GeneratedBufferKind::QueueBacking);
  assert(plan.generated[0].layout == GeneratedDmaLayout::PageList);
  assert(plan.generated[0].logicalBytes == 0x81000ull);
  assert(plan.generated[0].allocationBytes == 0x81000ull);
  assert(plan.generated[0].pageCount == 129u);

  assert(plan.generated[1].kind == GeneratedBufferKind::CachedArguments);
  assert(plan.generated[1].layout == GeneratedDmaLayout::Linear);
  assert(plan.generated[1].logicalBytes == 72u);
  assert(plan.generated[1].allocationBytes == 0x1000u);

  assert(plan.generated[2].kind == GeneratedBufferKind::LibosInitArguments);
  assert(plan.generated[2].allocationBytes == 0x1000u);
  assert(plan.generated[3].kind == GeneratedBufferKind::WprMetadata);
  assert(plan.generated[3].logicalBytes == 256u);
  assert(plan.generated[3].allocationBytes == 0x1000u);

  assert(plan.generated[4].kind == GeneratedBufferKind::Radix3Firmware);
  assert(plan.generated[4].layout == GeneratedDmaLayout::PageList);
  assert(plan.generated[4].logicalBytes ==
         profile.manifest.radix3.allocationBytes);
  assert(plan.generated[4].allocationBytes ==
         profile.manifest.radix3.allocationPages * kBootPreparePageBytes);
  assert(plan.generated[4].allocationBytes >= plan.generated[4].logicalBytes);
  assert(plan.generated[4].allocationBytes > profile.manifest.inputs.gspFirmwareImageBytes);

  for (std::size_t i = 5u; i < plan.generated.size(); ++i) {
    assert(plan.generated[i].layout == GeneratedDmaLayout::Linear);
    assert(plan.generated[i].logicalBytes == kLibosLogRegionBytes);
    assert(plan.generated[i].allocationBytes == kLibosLogRegionBytes);
    assert(plan.generated[i].pageCount == 16u);
  }

  assert(plan.reusedSignatureAllocationBytes == 0x1000u);
  assert(plan.reusedBootloaderAllocationBytes == 0x18000u);

  assert(plan.framebuffer.frtsRegionOffset == 0x1FFE00000ull);
  assert(plan.framebuffer.frtsRegionBytes == 0x100000ull);
  assert(plan.framebuffer.sec2BooterBytes == 0x11000ull);
  assert(plan.framebuffer.frtsFwsecBytes == 0x21000ull);
  assert(plan.framebuffer.sec2BooterOffset + plan.framebuffer.sec2BooterBytes ==
         profile.manifest.wpr.gspFwRsvdStart);
  assert(plan.framebuffer.frtsFwsecOffset + plan.framebuffer.frtsFwsecBytes ==
         plan.framebuffer.sec2BooterOffset);
  assert(plan.framebuffer.frtsFwsecOffset < profile.manifest.wpr.gspFwRsvdStart);

  std::uint64_t expectedTotal = 0u;
  for (const auto& requirement : plan.generated)
    expectedTotal += requirement.allocationBytes;
  assert(plan.totalGeneratedAllocationBytes == expectedTotal);

  auto invalid = profile;
  invalid.status = ProfileStatus::ManifestRejected;
  assert(PlanGa10xBootPreparation(invalid).status ==
         BootPreparationPlanStatus::InvalidProfile);

  auto brokenManifest = profile;
  brokenManifest.manifest.allocations.clear();
  assert(PlanGa10xBootPreparation(brokenManifest).status ==
         BootPreparationPlanStatus::InvalidManifest);

  auto mismatchedFrts = profile;
  mismatchedFrts.manifest.wpr.frtsOffset += 0x1000u;
  assert(PlanGa10xBootPreparation(mismatchedFrts).status ==
         BootPreparationPlanStatus::FrtsPlacementMismatch);

  std::cout << "rtxmac GA10x cold boot preparation plan tests passed\n";
}
