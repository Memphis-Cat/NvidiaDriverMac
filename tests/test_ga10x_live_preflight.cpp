#include "rtxmac/ga10x_live_preflight.hpp"

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
  view.metadata.gspAppVersion = 1u;
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
      {SectionKind::GspFirmwareImage, 0x1000u, 0x200000ull, {}},
      {SectionKind::GspFirmwareSignature, 0x201000u, 0x1000ull, {}},
      {SectionKind::GspBootloader, 0x202000u, 0x18000ull, {}},
      {SectionKind::FrtsFwsecImage, 0x21A000u, 0x20000ull, {}},
      {SectionKind::Sec2BooterImage, 0x23A000u, 0x10000ull, {}},
  }};
  return view;
}

} // namespace

int main() {
  using namespace rtxmac::nvidia;
  using namespace rtxmac::nvidia::prototype;

  const auto profile = BuildGa10xPrototypeProfile(MakePackage());
  assert(profile.status == ProfileStatus::Ok);
  assert(profile.manifestInputs.vgaWorkspaceOffset == 0x1FFF00000ull);
  assert(profile.manifestInputs.vbiosReservedOffset == 0x1FFF00000ull);

  const auto missing = CheckReservedBoundary(profile, std::nullopt);
  assert(missing.status == ReservedBoundaryStatus::MmuLockUnavailable);

  const MmuLockState unreadable{
      .readable = false, .valid = false, .low = 0u, .high = 0u};
  assert(CheckReservedBoundary(profile, unreadable).status ==
         ReservedBoundaryStatus::MmuLockUnreadable);

  // Readable but invalid/inverted means NVIDIA's HAL treats the lock as absent.
  const MmuLockState absent{
      .readable = true,
      .valid = false,
      .low = 0x1FFFFF000ull,
      .high = 0x1FF000000ull};
  const auto noLock = CheckReservedBoundary(profile, absent);
  assert(noLock.status == ReservedBoundaryStatus::Ok);
  assert(!noLock.activeMmuLock);
  assert(noLock.effectiveBoundary == 0x1FFF00000ull);

  // A valid lock beginning above the VGA workspace does not lower the boundary.
  const MmuLockState harmless{
      .readable = true,
      .valid = true,
      .low = 0x1FFF80000ull,
      .high = 0x1FFFFFFF0ull};
  const auto okay = CheckReservedBoundary(profile, harmless);
  assert(okay.status == ReservedBoundaryStatus::Ok);
  assert(okay.activeMmuLock);
  assert(okay.effectiveBoundary == 0x1FFF00000ull);

  // A lower VBIOS lock changes the production vbiosReservedOffset. The offline
  // package/profile must be rebuilt; do not merely patch WPR metadata in place.
  const MmuLockState lower{
      .readable = true,
      .valid = true,
      .low = 0x1FF000000ull,
      .high = 0x1FFFFF000ull};
  const auto rebuild = CheckReservedBoundary(profile, lower);
  assert(rebuild.status == ReservedBoundaryStatus::RebuildRequired);
  assert(rebuild.activeMmuLock);
  assert(rebuild.effectiveBoundary == 0x1FF000000ull);

  auto invalidProfile = profile;
  invalidProfile.status = ProfileStatus::ManifestRejected;
  assert(CheckReservedBoundary(invalidProfile, harmless).status ==
         ReservedBoundaryStatus::InvalidProfile);

  std::cout << "rtxmac GA10x live reserved-boundary preflight tests passed\n";
}
