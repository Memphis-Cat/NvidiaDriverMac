#include "rtxmac/gsp_manifest.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
  using namespace rtxmac::nvidia;
  using namespace rtxmac::nvidia::gsp;

  ManifestInputs in{
    .fbSize = 0x200000000ull,
    .vgaWorkspaceOffset = 0x1FFF00000ull,
    .vbiosReservedOffset = 0x1FFF00000ull,
    .wprEndMargin = 0u,
    .frtsSize = 0x100000ull,
    .nonWprHeapSize = 0x100000ull,
    .requestedWprHeapSize = 0x8100000ull,
    .gspFirmwareImageBytes = 0x1A34000ull,
    .gspSignatureBytes = 0x1000ull,
    .gspBootloaderBytes = 0x18000ull,
    .frtsFwsecImageBytes = 0x20000ull,
    .sec2BooterImageBytes = 0x10000ull,
  };
  const auto m = PlanBootManifest(in);
  assert(m.valid && m.allocations.size() == 9u);
  assert(m.queues.totalBytes == 0x81000u);
  assert(m.radix3.imageBytes == in.gspFirmwareImageBytes);
  assert(!m.bootstrapRpcPrefillImplemented);

  ResolvedAddresses a{
    .queueBacking = 0x10000000ull,
    .cachedArguments = 0x10100000ull,
    .libosInitArguments = 0x10200000ull,
    .wprMetadata = 0x10300000ull,
    .radix3FirmwareRoot = 0x10400000ull,
    .firmwareSignature = 0x14000000ull,
    .gspBootloader = 0x14100000ull,
    .frtsFwsecImage = 0x1F0000000ull,
    .sec2BooterImage = 0x1F0100000ull,
  };

  fw::RiscvBootloaderInfo riscv{};
  riscv.status = fw::ParseStatus::Ok;
  riscv.bin.dataSize = static_cast<std::uint32_t>(in.gspBootloaderBytes);
  riscv.descriptor.monitorCodeOffset = 0x1000;
  riscv.descriptor.monitorDataOffset = 0x9000;
  riscv.descriptor.manifestOffset = 0x200;
  const std::array<LibosRegion,1> regions{{{"RMARGS", a.cachedArguments, 0x1000}}};
  const auto artifacts = BuildResolvedArtifacts(m, a, riscv, regions);
  assert(artifacts.has_value());

  vbios::DescriptorV3 fwsec{};
  fwsec.storedSize = 0x20000;
  fwsec.pkcDataOffset = 0x100;
  fwsec.imemPhysBase = 0;
  fwsec.imemVirtBase = 0;
  fwsec.imemLoadSize = 0x1000;
  fwsec.dmemPhysBase = 0;
  fwsec.dmemLoadSize = 0x1000;
  fwsec.engineIdMask = 1;
  fwsec.ucodeId = 5;

  fw::BooterImageInfo sec2{};
  sec2.status = fw::ParseStatus::Ok;
  sec2.bin.dataSize = static_cast<std::uint32_t>(in.sec2BooterImageBytes);
  sec2.firstApp.offset = 0x1000;
  sec2.firstApp.size = 0x1000;
  sec2.load.osDataOffset = 0x3000;
  sec2.load.osDataSize = 0x1000;

  const auto seq = PlanBootSequence(m, a, fwsec, sec2, 0x174u);
  assert(seq.valid);
  assert(!seq.executableWithCurrentCore);
  assert(seq.phases.size() == 12u);
  assert(seq.phases.front().phase == BootPhase::PrefillBootstrapRpcRecords);
  assert(seq.phases.front().checks.size() == 1u);
  assert(seq.phases.front().checks[0].kind == CheckKind::HostPreparationRequired);
  assert(seq.phases[3].phase == BootPhase::VerifyWpr2);
  assert(seq.phases[3].checks[0].addressOrOffset == 0x001FA828u);
  assert(seq.phases[10].phase == BootPhase::VerifyGspRiscv);
  assert(seq.phases[11].phase == BootPhase::WaitStatusQueue);
  assert(seq.phases[11].checks[0].addressOrOffset == m.queues.statusQueueOffset + 28u);

  auto badA = a; badA.wprMetadata += 1;
  assert(!BuildResolvedArtifacts(m, badA, riscv, regions).has_value());
  assert(!PlanBootSequence(m, badA, fwsec, sec2, 0x174u).valid);

  std::cout << "rtxmac complete GSP boot-manifest tests passed\n";
}
