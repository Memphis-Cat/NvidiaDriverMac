#include "rtxmac/ga10x_prototype.hpp"

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
      {SectionKind::FrtsFwsecImage, 0x21B000u, 0x20000ull, {}},
      {SectionKind::Sec2BooterImage, 0x23B000u, 0x10000ull, {}},
  }};
  return view;
}

} // namespace

int main() {
  using namespace rtxmac::nvidia::prototype;

  const auto package = MakePackage();
  const auto profile = BuildGa10xPrototypeProfile(package);
  assert(profile.status == ProfileStatus::Ok);
  assert(profile.assumesNoLowerVbiosMmuLock);
  assert(profile.manifest.valid);
  assert(profile.manifestInputs.fbSize == 0x200000000ull);
  assert(profile.manifestInputs.vgaWorkspaceOffset == 0x1FFF00000ull);
  assert(profile.manifestInputs.vbiosReservedOffset == 0x1FFF00000ull);
  assert(profile.manifestInputs.frtsSize == 0x100000ull);
  assert(profile.manifestInputs.nonWprHeapSize == 0x100000ull);
  assert(profile.manifestInputs.requestedWprHeapSize == 0x8100000ull);
  assert(profile.manifest.wpr.frtsOffset == 0x1FFE00000ull);
  assert(profile.manifest.wpr.gspFwWprEnd == 0x1FFF00000ull);
  assert(profile.manifest.wpr.vgaWorkspaceSize == 0x100000ull);
  assert(profile.gspBootloader.status == rtxmac::nvidia::fw::ParseStatus::Ok);
  assert(profile.gspBootloader.bin.dataSize == 0x18000u);
  assert(profile.gspBootloader.descriptor.appVersion == 0x12345678u);
  assert(profile.fwsec.pkcDataOffset == 0x100u);
  assert(profile.fwsec.imemLoadSize == 0x1000u);
  assert(profile.sec2Booter.bin.dataSize == 0x10000u);
  assert(profile.sec2Booter.firstApp.offset == 0x1000u);
  assert(profile.sec2Booter.load.osDataOffset == 0x4000u);

  const auto liveBoundaryProfile =
      BuildGa10xPrototypeProfile(package, 0x1FF000000ull);
  assert(liveBoundaryProfile.status == ProfileStatus::Ok);
  assert(!liveBoundaryProfile.assumesNoLowerVbiosMmuLock);
  assert(liveBoundaryProfile.manifestInputs.vgaWorkspaceOffset ==
         0x1FFF00000ull);
  assert(liveBoundaryProfile.manifestInputs.vbiosReservedOffset ==
         0x1FF000000ull);
  assert(liveBoundaryProfile.manifest.wpr.gspFwWprEnd == 0x1FF000000ull);
  assert(liveBoundaryProfile.manifest.wpr.frtsOffset == 0x1FEF00000ull);

  assert(BuildGa10xPrototypeProfile(package, 0x1FFF01000ull).status ==
         ProfileStatus::InvalidReservedBoundary);
  assert(BuildGa10xPrototypeProfile(package, 0x1FF000001ull).status ==
         ProfileStatus::InvalidReservedBoundary);

  auto unsupported = package;
  unsupported.metadata.pci.device = 0x2504u;
  assert(BuildGa10xPrototypeProfile(unsupported).status ==
         ProfileStatus::UnsupportedTarget);

  auto tinyVram = package;
  tinyVram.metadata.vramBytes = 0x02000000ull;
  assert(BuildGa10xPrototypeProfile(tinyVram).status ==
         ProfileStatus::InvalidVram);

  auto missing = package;
  missing.sections[4].size = 0u;
  assert(BuildGa10xPrototypeProfile(missing).status ==
         ProfileStatus::MissingSection);

  auto badMeta = package;
  badMeta.metadata.gspAppVersion = 0u;
  assert(BuildGa10xPrototypeProfile(badMeta).status ==
         ProfileStatus::InvalidMetadata);

  auto unverified = package;
  unverified.status = rtxmac::nvidia::package::ParseStatus::HashMismatch;
  assert(BuildGa10xPrototypeProfile(unverified).status ==
         ProfileStatus::PackageNotVerified);

  std::cout << "rtxmac GA10x prototype profile tests passed\n";
}
