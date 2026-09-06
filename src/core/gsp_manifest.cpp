#include "rtxmac/gsp_manifest.hpp"

#include <limits>
#include <utility>

namespace rtxmac::nvidia::gsp {
namespace {
constexpr std::uint64_t kPage=0x1000ull;
constexpr std::uint32_t kWpr2Hi=0x001FA828u, kGspMailbox0=0x00110040u, kGspMailbox1=0x00110044u, kGspFalconOs=0x00110080u, kSec2Mailbox0=0x00840040u, kGspRiscvCpuCtl=0x00111388u;
constexpr std::uint32_t kRiscvActiveMask=1u<<7u; constexpr std::uint64_t kStatusQueueEntryOffField=28ull; constexpr std::uint32_t kExpectedQueueEntryOff=0x1000u;
std::optional<std::uint64_t> AlignUp(std::uint64_t v,std::uint64_t a) noexcept { if(!a)return std::nullopt;auto r=v%a;if(!r)return v;auto d=a-r;if(v>std::numeric_limits<std::uint64_t>::max()-d)return std::nullopt;return v+d; }
bool PageAligned(std::uint64_t v) noexcept{return (v&(kPage-1u))==0u;}
void AddAlloc(BootManifest& o,AllocationKind k,MemoryDomain d,std::uint64_t l,std::uint64_t a,DmaLayoutRequirement dl){auto x=AlignUp(l,a);if(!x){o.valid=false;return;}o.allocations.push_back({k,d,l,*x,a,dl!=DmaLayoutRequirement::None,dl});}
void AppendActions(PhasePlan& p,const falcon::Plan& x){p.actions.insert(p.actions.end(),x.actions.begin(),x.actions.end());}
falcon::Action Write32(std::uint32_t a,std::uint32_t v){return {falcon::ActionKind::Write32,a,v,0xFFFFFFFFu,0u};}
bool AddressesOk(const ResolvedAddresses&a) noexcept{return PageAligned(a.queueBacking)&&PageAligned(a.cachedArguments)&&PageAligned(a.libosInitArguments)&&PageAligned(a.wprMetadata)&&PageAligned(a.radix3FirmwareRoot)&&PageAligned(a.firmwareSignature)&&PageAligned(a.gspBootloader)&&PageAligned(a.frtsFwsecImage)&&PageAligned(a.sec2BooterImage);}

const AllocationRequirement* FindAllocation(const BootManifest& manifest, AllocationKind kind) noexcept {
  for (const auto& requirement : manifest.allocations) {
    if (requirement.kind == kind) return &requirement;
  }
  return nullptr;
}

bool MakeFramebufferStageImage(const BootManifest& manifest,
                               AllocationKind kind,
                               std::uint64_t vramOffset,
                               FramebufferStageImage& out) noexcept {
  const AllocationRequirement* requirement = FindAllocation(manifest, kind);
  if (!requirement || requirement->domain != MemoryDomain::Framebuffer ||
      requirement->requiresDmaMapping || requirement->dmaLayout != DmaLayoutRequirement::None ||
      requirement->logicalBytes == 0 || requirement->allocationBytes == 0 ||
      requirement->alignment == 0 || (vramOffset % requirement->alignment) != 0u) {
    return false;
  }

  if (manifest.wpr.fbSize == 0 || manifest.wpr.gspFwRsvdStart > manifest.wpr.fbSize ||
      vramOffset >= manifest.wpr.fbSize ||
      requirement->allocationBytes > manifest.wpr.fbSize - vramOffset) {
    return false;
  }
  const std::uint64_t end = vramOffset + requirement->allocationBytes;

  // Temporary source images must not occupy NVIDIA's GSP reserved/WPR/VBIOS
  // tail. Falcon DMA reads them from framebuffer memory before that tail is
  // used for the protected runtime layout.
  if (end > manifest.wpr.gspFwRsvdStart) return false;

  auto pramin = rtxmac::nvidia::PlanPraminStage(
      vramOffset, requirement->allocationBytes, manifest.wpr.fbSize);
  if (!pramin.valid) return false;

  out = FramebufferStageImage{
      .kind = kind,
      .vramOffset = vramOffset,
      .logicalBytes = requirement->logicalBytes,
      .allocationBytes = requirement->allocationBytes,
      .pramin = std::move(pramin),
  };
  return true;
}

bool RangesOverlap(std::uint64_t aStart, std::uint64_t aBytes,
                   std::uint64_t bStart, std::uint64_t bBytes) noexcept {
  // Inputs were already proven to fit in framebuffer, so additions are safe.
  const std::uint64_t aEnd = aStart + aBytes;
  const std::uint64_t bEnd = bStart + bBytes;
  return aStart < bEnd && bStart < aEnd;
}
}

std::optional<ResolvedDmaAllocation> ResolveSystemDmaAllocation(
    const AllocationRequirement& requirement,
    std::span<const rtxmac::DmaSegment> segments) noexcept {
  if (requirement.domain != MemoryDomain::System || !requirement.requiresDmaMapping ||
      requirement.dmaLayout == DmaLayoutRequirement::None ||
      requirement.allocationBytes == 0 || requirement.alignment == 0) {
    return std::nullopt;
  }

  ResolvedDmaAllocation out{};
  out.kind = requirement.kind;
  out.layout = requirement.dmaLayout;
  out.allocationBytes = requirement.allocationBytes;

  if (requirement.dmaLayout == DmaLayoutRequirement::Linear) {
    const auto linear = rtxmac::ResolveLinearDmaRange(segments, requirement.allocationBytes);
    if (linear.status != rtxmac::DmaLinearRangeStatus::Ok ||
        linear.length != requirement.allocationBytes ||
        (linear.address % requirement.alignment) != 0u) {
      return std::nullopt;
    }
    out.baseAddress = linear.address;
    return out;
  }

  if (requirement.dmaLayout == DmaLayoutRequirement::PageList) {
    // Current Ampere queue/Radix3 formats use 4 KiB physical pages. Keep this
    // independent of a larger allocation alignment so the ABI stays explicit.
    const auto pages = rtxmac::ExpandDmaSegmentsToPages(
        segments, requirement.allocationBytes, kPage);
    if (pages.status != rtxmac::DmaPageMapStatus::Ok || pages.pageAddresses.empty() ||
        (pages.pageAddresses.front() % requirement.alignment) != 0u) {
      return std::nullopt;
    }
    out.baseAddress = pages.pageAddresses.front();
    out.pageAddresses = pages.pageAddresses;
    return out;
  }

  return std::nullopt;
}

BootManifest PlanBootManifest(const ManifestInputs& in){
  BootManifest o{};o.inputs=in;if(!in.gspFirmwareImageBytes||!in.gspSignatureBytes||!in.gspBootloaderBytes||!in.frtsFwsecImageBytes||!in.sec2BooterImageBytes)return o;
  auto q=PlanQueueMemory(in.queueBytes);auto r=PlanRadix3(in.gspFirmwareImageBytes);auto w=PlanWprLayout({in.fbSize,in.vgaWorkspaceOffset,in.vbiosReservedOffset,in.wprEndMargin,in.frtsSize,in.gspBootloaderBytes,in.gspFirmwareImageBytes,in.nonWprHeapSize,in.requestedWprHeapSize});
  if(!q||!r||!w)return o;o.queues=*q;o.radix3=*r;o.wpr=*w;o.valid=true;o.bootstrapRpcPrefillImplemented=true;

  // Queue backing and the GSP firmware have explicit page indirection and may
  // be genuinely scattered. Every other system-memory handoff exposes only a
  // base address plus size and therefore must resolve to one GPU-linear range.
  AddAlloc(o,AllocationKind::QueueBacking,MemoryDomain::System,q->totalBytes,kPage,DmaLayoutRequirement::PageList);
  AddAlloc(o,AllocationKind::CachedArguments,MemoryDomain::System,kGspArgumentsCachedBytes,kPage,DmaLayoutRequirement::Linear);
  AddAlloc(o,AllocationKind::LibosInitArguments,MemoryDomain::System,kLibosInitPageBytes,kPage,DmaLayoutRequirement::Linear);
  AddAlloc(o,AllocationKind::WprMetadata,MemoryDomain::System,kWprMetaBytes,kPage,DmaLayoutRequirement::Linear);
  AddAlloc(o,AllocationKind::Radix3Firmware,MemoryDomain::System,r->allocationBytes,kPage,DmaLayoutRequirement::PageList);
  AddAlloc(o,AllocationKind::FirmwareSignature,MemoryDomain::System,in.gspSignatureBytes,kPage,DmaLayoutRequirement::Linear);
  AddAlloc(o,AllocationKind::GspBootloader,MemoryDomain::System,in.gspBootloaderBytes,kPage,DmaLayoutRequirement::Linear);
  AddAlloc(o,AllocationKind::FrtsFwsecImage,MemoryDomain::Framebuffer,in.frtsFwsecImageBytes,kPage,DmaLayoutRequirement::None);
  AddAlloc(o,AllocationKind::Sec2BooterImage,MemoryDomain::Framebuffer,in.sec2BooterImageBytes,kPage,DmaLayoutRequirement::None);
  return o;
}

FramebufferStagingPlan PlanFramebufferStaging(
    const BootManifest& manifest,
    const ResolvedAddresses& addresses) noexcept {
  FramebufferStagingPlan out{};
  if (!manifest.valid || !AddressesOk(addresses)) return out;

  FramebufferStageImage frts{};
  FramebufferStageImage sec2{};
  if (!MakeFramebufferStageImage(
          manifest, AllocationKind::FrtsFwsecImage, addresses.frtsFwsecImage, frts) ||
      !MakeFramebufferStageImage(
          manifest, AllocationKind::Sec2BooterImage, addresses.sec2BooterImage, sec2)) {
    return out;
  }

  if (RangesOverlap(frts.vramOffset, frts.allocationBytes,
                    sec2.vramOffset, sec2.allocationBytes)) {
    return out;
  }

  out.images.push_back(std::move(frts));
  out.images.push_back(std::move(sec2));
  out.valid = true;
  return out;
}

std::optional<ResolvedArtifacts> BuildResolvedArtifacts(const BootManifest&m,const ResolvedAddresses&a,const fw::RiscvBootloaderInfo& bl,std::span<const std::uint64_t> queuePages,std::span<const std::uint64_t> radixPages,std::span<const std::uint8_t> firmware,std::span<const LibosRegion> regions,const GspSystemInfoInputs& sys,std::span<const RegistryDwordEntry> registry) noexcept{
  if(!m.valid||bl.status!=fw::ParseStatus::Ok||!AddressesOk(a)||bl.bin.dataSize!=m.inputs.gspBootloaderBytes)return std::nullopt;
  if(queuePages.empty()||queuePages.size()!=m.queues.pageTableEntryCount||queuePages.front()!=a.queueBacking)return std::nullopt;
  if(radixPages.empty()||radixPages.size()!=m.radix3.allocationPages||radixPages.front()!=a.radix3FirmwareRoot)return std::nullopt;
  if(firmware.size()!=m.inputs.gspFirmwareImageBytes||firmware.size()!=m.radix3.imageBytes)return std::nullopt;

  auto lib=BuildLibosInitPage(regions);if(!lib)return std::nullopt;
  auto shared=BuildSharedQueueAllocationImage(m.queues,queuePages,sys,registry);if(!shared)return std::nullopt;
  auto radix=BuildRadix3AllocationImage(m.radix3,radixPages,firmware);if(!radix)return std::nullopt;

  ResolvedArtifacts o{};
  o.cachedArguments=shared->cachedArguments;
  o.libosInitArguments=*lib;
  o.sharedQueueAllocation=std::move(shared->bytes);
  o.radix3FirmwareAllocation=std::move(*radix);
  o.bootstrapCommandQueue=std::move(shared->commandQueue.bytes);
  o.wprMetadata=BuildWprMeta({m.wpr,a.radix3FirmwareRoot,a.gspBootloader,bl.descriptor.monitorCodeOffset,bl.descriptor.monitorDataOffset,bl.descriptor.manifestOffset,a.firmwareSignature,m.inputs.gspSignatureBytes,0u,0u,0u});
  return o;
}

BootSequence PlanBootSequence(const BootManifest&m,const ResolvedAddresses&a,const vbios::DescriptorV3& f,const fw::BooterImageInfo& s,std::uint32_t chip){
  BootSequence o{};if(!m.valid||s.status!=fw::ParseStatus::Ok||!AddressesOk(a)||s.bin.dataSize!=m.inputs.sec2BooterImageBytes||!f.imemLoadSize||!f.dmemLoadSize)return o;
  o.phases.push_back({BootPhase::PrefillBootstrapRpcRecords,{},{}});
  PhasePlan p{BootPhase::ResetGspForFrts};AppendActions(p,falcon::PlanReset(falcon::Engine::Gsp,false,chip));o.phases.push_back(std::move(p));
  auto fx=falcon::PlanAuthenticatedExecution({falcon::Engine::Gsp,a.frtsFwsecImage,0u,f.imemLoadSize,f.imemPhysBase,f.imemVirtBase,f.imemLoadSize,f.dmemPhysBase,0u,f.dmemLoadSize,f.pkcDataOffset,f.engineIdMask,f.ucodeId,std::nullopt});if(!fx.valid)return {};p={BootPhase::ExecuteFrtsFwsec};AppendActions(p,fx);o.phases.push_back(std::move(p));
  o.phases.push_back({BootPhase::VerifyWpr2,{},{{CheckKind::MmioNonZero,kWpr2Hi,0xFFFFFFFFu,0u}}});p={BootPhase::ResetGspForRiscv};AppendActions(p,falcon::PlanReset(falcon::Engine::Gsp,true,chip));o.phases.push_back(std::move(p));
  o.phases.push_back({BootPhase::ProgramLibosMailbox,{Write32(kGspMailbox0,static_cast<std::uint32_t>(a.libosInitArguments)),Write32(kGspMailbox1,static_cast<std::uint32_t>(a.libosInitArguments>>32u))},{}});p={BootPhase::ResetSec2};AppendActions(p,falcon::PlanReset(falcon::Engine::Sec2,false,chip));o.phases.push_back(std::move(p));
  auto sx=falcon::PlanAuthenticatedExecution({falcon::Engine::Sec2,a.sec2BooterImage,s.firstApp.offset,s.load.osDataOffset,0u,s.firstApp.offset,s.firstApp.size,0u,0u,s.load.osDataSize,0x10u,1u,3u,a.wprMetadata});if(!sx.valid)return {};p={BootPhase::ExecuteSec2Booter};AppendActions(p,sx);o.phases.push_back(std::move(p));
  o.phases.push_back({BootPhase::VerifySec2Booter,{},{{CheckKind::MmioMaskEqual,kSec2Mailbox0,0xFFFFFFFFu,0u}}});o.phases.push_back({BootPhase::ReleaseGspRiscv,{Write32(kGspFalconOs,0u)},{}});o.phases.push_back({BootPhase::VerifyGspRiscv,{},{{CheckKind::MmioMaskEqual,kGspRiscvCpuCtl,kRiscvActiveMask,kRiscvActiveMask}}});o.phases.push_back({BootPhase::WaitStatusQueue,{},{{CheckKind::SharedMemoryU32Equal,m.queues.statusQueueOffset+kStatusQueueEntryOffField,0xFFFFFFFFu,kExpectedQueueEntryOff}}});o.valid=true;o.executableWithCurrentCore=m.bootstrapRpcPrefillImplemented;return o;
}

} // namespace rtxmac::nvidia::gsp
