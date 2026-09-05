#include "rtxmac/gsp_manifest.hpp"

#include <array>
#include <cassert>
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
  auto m = PlanBootManifest(in);
  assert(m.valid && m.allocations.size() == 9u && m.bootstrapRpcPrefillImplemented);

  assert(m.allocations[0].kind == AllocationKind::QueueBacking);
  assert(m.allocations[0].requiresDmaMapping);
  assert(m.allocations[0].dmaLayout == DmaLayoutRequirement::PageList);
  assert(m.allocations[1].dmaLayout == DmaLayoutRequirement::Linear); // cached args
  assert(m.allocations[2].dmaLayout == DmaLayoutRequirement::Linear); // libOS args
  assert(m.allocations[3].dmaLayout == DmaLayoutRequirement::Linear); // WPR metadata
  assert(m.allocations[4].kind == AllocationKind::Radix3Firmware);
  assert(m.allocations[4].dmaLayout == DmaLayoutRequirement::PageList);
  assert(m.allocations[5].dmaLayout == DmaLayoutRequirement::Linear); // signature
  assert(m.allocations[6].dmaLayout == DmaLayoutRequirement::Linear); // bootloader
  assert(!m.allocations[7].requiresDmaMapping && m.allocations[7].dmaLayout == DmaLayoutRequirement::None);
  assert(!m.allocations[8].requiresDmaMapping && m.allocations[8].dmaLayout == DmaLayoutRequirement::None);

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

  fw::RiscvBootloaderInfo r{};
  r.status = fw::ParseStatus::Ok;
  r.bin.dataSize = static_cast<std::uint32_t>(in.gspBootloaderBytes);
  r.descriptor.monitorCodeOffset = 0x1000;
  r.descriptor.monitorDataOffset = 0x9000;
  r.descriptor.manifestOffset = 0x200;

  std::array<LibosRegion,1> lr{{{"RMARGS", a.cachedArguments, 0x1000}}};
  auto defs = DefaultBootstrapRegistry();
  GspSystemInfoInputs si{
    .bar0Physical = 0x800000000ull,
    .bar1Physical = 0x900000000ull,
    .bar3Physical = 0xA00000000ull,
    .domainBusDeviceFunction = 0x100,
    .pciDeviceIdDword = 0x248910DE,
    .pciSubDeviceIdDword = 0x123410DE,
    .pciRevisionId = 0xA1,
  };
  auto art = BuildResolvedArtifacts(m, a, r, lr, si, defs);
  assert(art && art->bootstrapCommandQueue.size() == 0x40000u);

  vbios::DescriptorV3 f{};
  f.pkcDataOffset = 0x100;
  f.imemLoadSize = 0x1000;
  f.dmemLoadSize = 0x1000;
  f.engineIdMask = 1;
  f.ucodeId = 5;

  fw::BooterImageInfo s{};
  s.status = fw::ParseStatus::Ok;
  s.bin.dataSize = static_cast<std::uint32_t>(in.sec2BooterImageBytes);
  s.firstApp.offset = 0x1000;
  s.firstApp.size = 0x1000;
  s.load.osDataOffset = 0x3000;
  s.load.osDataSize = 0x1000;

  auto seq = PlanBootSequence(m, a, f, s, 0x174);
  assert(seq.valid && seq.executableWithCurrentCore && seq.phases.size() == 12u);
  assert(seq.phases[0].checks.empty());
  assert(seq.phases[3].checks[0].addressOrOffset == 0x001FA828u);

  auto bad = a;
  bad.wprMetadata++;
  assert(!BuildResolvedArtifacts(m, bad, r, lr, si, defs));
  assert(!PlanBootSequence(m, bad, f, s, 0x174).valid);

  std::cout << "rtxmac complete GSP boot-manifest tests passed\n";
}
