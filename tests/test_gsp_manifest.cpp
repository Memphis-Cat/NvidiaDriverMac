#include "rtxmac/gsp_manifest.hpp"

#include <array>
#include <cassert>
#include <iostream>
#include <vector>

namespace {
std::uint64_t L64(const auto& v, std::size_t o) {
  std::uint64_t x=0; for(std::size_t i=0;i<8;++i)x|=static_cast<std::uint64_t>(v[o+i])<<(i*8u); return x;
}
}

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
    // 513 image pages (last partial) exercises a multi-page level-2 Radix3 tree.
    .gspFirmwareImageBytes = 0x200FEFull,
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

  // Page-list allocations may be genuinely fragmented as long as each page is
  // representable and the total mapping exactly covers the allocation.
  const std::vector<rtxmac::DmaSegment> queueSegments{
    {0x10000000ull, 0x20000ull},
    {0x30000000ull, 0x61000ull},
  };
  const auto queueDma = ResolveSystemDmaAllocation(m.allocations[0], queueSegments);
  assert(queueDma);
  assert(queueDma->layout == DmaLayoutRequirement::PageList);
  assert(queueDma->baseAddress == 0x10000000ull);
  assert(queueDma->pageAddresses.size() == 129u);
  assert(queueDma->pageAddresses[31] == 0x1001F000ull);
  assert(queueDma->pageAddresses[32] == 0x30000000ull);

  // Linear boot data may be split into multiple descriptors only when the GPU
  // IOVA ranges are adjacent and therefore still form one base+size range.
  const std::vector<rtxmac::DmaSegment> linearBootloader{
    {0x14100000ull, 0x8000ull},
    {0x14108000ull, 0x10000ull},
  };
  const auto bootloaderDma = ResolveSystemDmaAllocation(m.allocations[6], linearBootloader);
  assert(bootloaderDma);
  assert(bootloaderDma->layout == DmaLayoutRequirement::Linear);
  assert(bootloaderDma->baseAddress == 0x14100000ull);
  assert(bootloaderDma->allocationBytes == 0x18000ull);
  assert(bootloaderDma->pageAddresses.empty());

  const std::vector<rtxmac::DmaSegment> fragmentedBootloader{
    {0x14100000ull, 0x8000ull},
    {0x15100000ull, 0x10000ull},
  };
  assert(!ResolveSystemDmaAllocation(m.allocations[6], fragmentedBootloader));
  assert(!ResolveSystemDmaAllocation(m.allocations[7], {})); // framebuffer, not DriverKit DMA

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

  std::vector<std::uint64_t> queuePages;
  queuePages.reserve(static_cast<std::size_t>(m.queues.pageTableEntryCount));
  for(std::uint64_t i=0;i<m.queues.pageTableEntryCount;++i)
    queuePages.push_back(a.queueBacking+i*0x2000ull); // deliberately fragmented logical pages

  std::vector<std::uint64_t> radixPages;
  radixPages.reserve(static_cast<std::size_t>(m.radix3.allocationPages));
  for(std::uint64_t i=0;i<m.radix3.allocationPages;++i)
    radixPages.push_back(a.radix3FirmwareRoot+i*0x3000ull); // deliberately fragmented pages
  std::vector<std::uint8_t> firmware(static_cast<std::size_t>(in.gspFirmwareImageBytes),0u);
  firmware.front()=0x5Au;firmware.back()=0xA5u;

  fw::RiscvBootloaderInfo r{};
  r.status=fw::ParseStatus::Ok;
  r.bin.dataSize=static_cast<std::uint32_t>(in.gspBootloaderBytes);
  r.descriptor.monitorCodeOffset=0x1000;
  r.descriptor.monitorDataOffset=0x9000;
  r.descriptor.manifestOffset=0x200;

  std::array<LibosRegion,1> lr{{{"RMARGS",a.cachedArguments,0x1000}}};
  auto defs=DefaultBootstrapRegistry();
  GspSystemInfoInputs si{
    .bar0Physical=0x800000000ull,
    .bar1Physical=0x900000000ull,
    .bar3Physical=0xA00000000ull,
    .domainBusDeviceFunction=0x100,
    .pciDeviceIdDword=0x248910DE,
    .pciSubDeviceIdDword=0x123410DE,
    .pciRevisionId=0xA1,
  };

  auto art=BuildResolvedArtifacts(m,a,r,queuePages,radixPages,firmware,lr,si,defs);
  assert(art);
  assert(art->sharedQueueAllocation.size()==0x81000u);
  assert(art->bootstrapCommandQueue.size()==0x40000u);
  assert(L64(art->sharedQueueAllocation,0u)==queuePages[0]);
  assert(L64(art->sharedQueueAllocation,128u*8u)==queuePages[128]);
  assert(L64(art->cachedArguments,0u)==a.queueBacking);

  assert(art->radix3FirmwareAllocation.size()==static_cast<std::size_t>(m.radix3.allocationPages*kRadixPageBytes));
  assert(L64(art->radix3FirmwareAllocation,0u)==radixPages[1]);
  assert(art->radix3FirmwareAllocation[static_cast<std::size_t>(m.radix3.offsets[3])]==0x5Au);
  assert(art->radix3FirmwareAllocation[static_cast<std::size_t>(m.radix3.offsets[3]+in.gspFirmwareImageBytes-1u)]==0xA5u);
  assert(art->radix3FirmwareAllocation.back()==0u);

  auto wrongQueueRoot=queuePages;wrongQueueRoot[0]+=0x1000ull;
  assert(!BuildResolvedArtifacts(m,a,r,wrongQueueRoot,radixPages,firmware,lr,si,defs));
  auto wrongRadixRoot=radixPages;wrongRadixRoot[0]+=0x1000ull;
  assert(!BuildResolvedArtifacts(m,a,r,queuePages,wrongRadixRoot,firmware,lr,si,defs));
  auto tooFew=queuePages;tooFew.pop_back();
  assert(!BuildResolvedArtifacts(m,a,r,tooFew,radixPages,firmware,lr,si,defs));
  auto shortFirmware=firmware;shortFirmware.pop_back();
  assert(!BuildResolvedArtifacts(m,a,r,queuePages,radixPages,shortFirmware,lr,si,defs));

  vbios::DescriptorV3 f{};
  f.pkcDataOffset=0x100;
  f.imemLoadSize=0x1000;
  f.dmemLoadSize=0x1000;
  f.engineIdMask=1;
  f.ucodeId=5;
  fw::BooterImageInfo s{};
  s.status=fw::ParseStatus::Ok;
  s.bin.dataSize=static_cast<std::uint32_t>(in.sec2BooterImageBytes);
  s.firstApp.offset=0x1000;
  s.firstApp.size=0x1000;
  s.load.osDataOffset=0x3000;
  s.load.osDataSize=0x1000;

  auto seq=PlanBootSequence(m,a,f,s,0x174);
  assert(seq.valid&&seq.executableWithCurrentCore&&seq.phases.size()==12u);
  assert(seq.phases[0].checks.empty());
  assert(seq.phases[3].checks[0].addressOrOffset==0x001FA828u);

  auto bad=a;bad.wprMetadata++;
  assert(!BuildResolvedArtifacts(m,bad,r,queuePages,radixPages,firmware,lr,si,defs));
  assert(!PlanBootSequence(m,bad,f,s,0x174).valid);

  std::cout<<"rtxmac complete GSP boot-manifest tests passed\n";
}
