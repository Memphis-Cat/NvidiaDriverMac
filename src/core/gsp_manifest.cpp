#include "rtxmac/gsp_manifest.hpp"

#include <limits>

namespace rtxmac::nvidia::gsp {
namespace {

constexpr std::uint64_t kPage = 0x1000ull;
constexpr std::uint32_t kWpr2Hi = 0x001FA828u;
constexpr std::uint32_t kGspMailbox0 = 0x00110040u;
constexpr std::uint32_t kGspMailbox1 = 0x00110044u;
constexpr std::uint32_t kGspFalconOs = 0x00110080u;
constexpr std::uint32_t kSec2Mailbox0 = 0x00840040u;
constexpr std::uint32_t kGspRiscvCpuCtl = 0x00111388u;
constexpr std::uint32_t kRiscvActiveMask = 1u << 7u;
constexpr std::uint64_t kStatusQueueEntryOffField = 28ull;
constexpr std::uint32_t kExpectedQueueEntryOff = 0x1000u;

std::optional<std::uint64_t> AlignUp(std::uint64_t value, std::uint64_t alignment) noexcept {
  if (alignment == 0u) return std::nullopt;
  const auto rem = value % alignment;
  if (rem == 0u) return value;
  const auto add = alignment - rem;
  if (value > std::numeric_limits<std::uint64_t>::max() - add) return std::nullopt;
  return value + add;
}

bool PageAligned(std::uint64_t value) noexcept { return (value & (kPage - 1u)) == 0u; }

void AddAlloc(BootManifest& out, AllocationKind kind, MemoryDomain domain,
              std::uint64_t logical, std::uint64_t alignment, bool dma) {
  const auto alloc = AlignUp(logical, alignment);
  if (!alloc) { out.valid = false; return; }
  out.allocations.push_back({kind, domain, logical, *alloc, alignment, dma});
}

void AppendActions(PhasePlan& phase, const falcon::Plan& plan) {
  phase.actions.insert(phase.actions.end(), plan.actions.begin(), plan.actions.end());
}

falcon::Action Write32(std::uint32_t address, std::uint32_t value) {
  return {falcon::ActionKind::Write32, address, value, 0xFFFFFFFFu, 0u};
}

bool ResolvedPageAddressesValid(const ResolvedAddresses& a) noexcept {
  return PageAligned(a.queueBacking) && PageAligned(a.cachedArguments) &&
      PageAligned(a.libosInitArguments) && PageAligned(a.wprMetadata) &&
      PageAligned(a.radix3FirmwareRoot) && PageAligned(a.firmwareSignature) &&
      PageAligned(a.gspBootloader) && PageAligned(a.frtsFwsecImage) &&
      PageAligned(a.sec2BooterImage);
}

} // namespace

BootManifest PlanBootManifest(const ManifestInputs& in) {
  BootManifest out{};
  out.inputs = in;
  if (in.gspFirmwareImageBytes == 0u || in.gspSignatureBytes == 0u ||
      in.gspBootloaderBytes == 0u || in.frtsFwsecImageBytes == 0u ||
      in.sec2BooterImageBytes == 0u) return out;

  const auto q = PlanQueueMemory(in.queueBytes);
  const auto r = PlanRadix3(in.gspFirmwareImageBytes);
  const auto w = PlanWprLayout({
    .fbSize = in.fbSize,
    .vgaWorkspaceOffset = in.vgaWorkspaceOffset,
    .vbiosReservedOffset = in.vbiosReservedOffset,
    .wprEndMargin = in.wprEndMargin,
    .frtsSize = in.frtsSize,
    .bootloaderSize = in.gspBootloaderBytes,
    .radix3ElfSize = in.gspFirmwareImageBytes,
    .nonWprHeapSize = in.nonWprHeapSize,
    .requestedWprHeapSize = in.requestedWprHeapSize,
  });
  if (!q || !r || !w) return out;
  out.queues = *q;
  out.radix3 = *r;
  out.wpr = *w;
  out.valid = true;

  AddAlloc(out, AllocationKind::QueueBacking, MemoryDomain::System, q->totalBytes, kPage, true);
  AddAlloc(out, AllocationKind::CachedArguments, MemoryDomain::System, kGspArgumentsCachedBytes, kPage, true);
  AddAlloc(out, AllocationKind::LibosInitArguments, MemoryDomain::System, kLibosInitPageBytes, kPage, true);
  AddAlloc(out, AllocationKind::WprMetadata, MemoryDomain::System, kWprMetaBytes, kPage, true);
  AddAlloc(out, AllocationKind::Radix3Firmware, MemoryDomain::System, r->allocationBytes, kPage, true);
  AddAlloc(out, AllocationKind::FirmwareSignature, MemoryDomain::System, in.gspSignatureBytes, kPage, true);
  AddAlloc(out, AllocationKind::GspBootloader, MemoryDomain::System, in.gspBootloaderBytes, kPage, true);
  AddAlloc(out, AllocationKind::FrtsFwsecImage, MemoryDomain::Framebuffer, in.frtsFwsecImageBytes, kPage, false);
  AddAlloc(out, AllocationKind::Sec2BooterImage, MemoryDomain::Framebuffer, in.sec2BooterImageBytes, kPage, false);
  return out;
}

std::optional<ResolvedArtifacts> BuildResolvedArtifacts(
    const BootManifest& manifest,
    const ResolvedAddresses& addresses,
    const fw::RiscvBootloaderInfo& bootloader,
    std::span<const LibosRegion> libosRegions) noexcept {
  if (!manifest.valid || bootloader.status != fw::ParseStatus::Ok || !ResolvedPageAddressesValid(addresses)) return std::nullopt;
  if (bootloader.bin.dataSize != manifest.inputs.gspBootloaderBytes) return std::nullopt;

  const auto libos = BuildLibosInitPage(libosRegions);
  if (!libos) return std::nullopt;

  ResolvedArtifacts out{};
  out.cachedArguments = BuildCachedArguments(manifest.queues, addresses.queueBacking);
  out.libosInitArguments = *libos;
  out.wprMetadata = BuildWprMeta({
    .layout = manifest.wpr,
    .sysmemAddrOfRadix3Elf = addresses.radix3FirmwareRoot,
    .sysmemAddrOfBootloader = addresses.gspBootloader,
    .bootloaderCodeOffset = bootloader.descriptor.monitorCodeOffset,
    .bootloaderDataOffset = bootloader.descriptor.monitorDataOffset,
    .bootloaderManifestOffset = bootloader.descriptor.manifestOffset,
    .sysmemAddrOfSignature = addresses.firmwareSignature,
    .sizeOfSignature = manifest.inputs.gspSignatureBytes,
    .gspFwHeapVfPartitionCount = 0u,
    .flags = 0u,
    .pmuReservedSize = 0u,
  });
  return out;
}

BootSequence PlanBootSequence(
    const BootManifest& manifest,
    const ResolvedAddresses& addresses,
    const vbios::DescriptorV3& fwsec,
    const fw::BooterImageInfo& sec2Booter,
    std::uint32_t chipId) {
  BootSequence out{};
  if (!manifest.valid || sec2Booter.status != fw::ParseStatus::Ok || !ResolvedPageAddressesValid(addresses)) return out;
  if (sec2Booter.bin.dataSize != manifest.inputs.sec2BooterImageBytes) return out;
  if (fwsec.imemLoadSize == 0u || fwsec.dmemLoadSize == 0u) return out;

  // This is deliberately surfaced first: queue payload construction still has
  // two host-side bootstrap RPC records to implement.
  PhasePlan prefill{BootPhase::PrefillBootstrapRpcRecords};
  if (!manifest.bootstrapRpcPrefillImplemented)
    prefill.checks.push_back({CheckKind::HostPreparationRequired, 0u, 0u, 0u});
  out.phases.push_back(std::move(prefill));

  const auto gspReset = falcon::PlanReset(falcon::Engine::Gsp, false, chipId);
  PhasePlan resetFrts{BootPhase::ResetGspForFrts};
  AppendActions(resetFrts, gspReset);
  out.phases.push_back(std::move(resetFrts));

  const auto frtsExec = falcon::PlanAuthenticatedExecution({
    .engine = falcon::Engine::Gsp,
    .imagePhysicalAddress = addresses.frtsFwsecImage,
    .codeOffset = 0u,
    .dataOffset = fwsec.imemLoadSize,
    .imemPhysicalBase = fwsec.imemPhysBase,
    .imemVirtualBase = fwsec.imemVirtBase,
    .imemBytes = fwsec.imemLoadSize,
    .dmemPhysicalBase = fwsec.dmemPhysBase,
    .dmemVirtualBase = 0u,
    .dmemBytes = fwsec.dmemLoadSize,
    .pkcOffset = fwsec.pkcDataOffset,
    .engineIdMask = fwsec.engineIdMask,
    .ucodeId = fwsec.ucodeId,
    .mailbox = std::nullopt,
  });
  if (!frtsExec.valid) return {};
  PhasePlan execFrts{BootPhase::ExecuteFrtsFwsec};
  AppendActions(execFrts, frtsExec);
  out.phases.push_back(std::move(execFrts));

  PhasePlan verifyWpr{BootPhase::VerifyWpr2};
  verifyWpr.checks.push_back({CheckKind::MmioNonZero, kWpr2Hi, 0xFFFFFFFFu, 0u});
  out.phases.push_back(std::move(verifyWpr));

  const auto gspRiscvReset = falcon::PlanReset(falcon::Engine::Gsp, true, chipId);
  PhasePlan resetRiscv{BootPhase::ResetGspForRiscv};
  AppendActions(resetRiscv, gspRiscvReset);
  out.phases.push_back(std::move(resetRiscv));

  PhasePlan mailbox{BootPhase::ProgramLibosMailbox};
  mailbox.actions.push_back(Write32(kGspMailbox0, static_cast<std::uint32_t>(addresses.libosInitArguments)));
  mailbox.actions.push_back(Write32(kGspMailbox1, static_cast<std::uint32_t>(addresses.libosInitArguments >> 32u)));
  out.phases.push_back(std::move(mailbox));

  const auto sec2Reset = falcon::PlanReset(falcon::Engine::Sec2, false, chipId);
  PhasePlan resetSec2{BootPhase::ResetSec2};
  AppendActions(resetSec2, sec2Reset);
  out.phases.push_back(std::move(resetSec2));

  const auto sec2Exec = falcon::PlanAuthenticatedExecution({
    .engine = falcon::Engine::Sec2,
    .imagePhysicalAddress = addresses.sec2BooterImage,
    .codeOffset = sec2Booter.firstApp.offset,
    .dataOffset = sec2Booter.load.osDataOffset,
    .imemPhysicalBase = 0u,
    .imemVirtualBase = sec2Booter.firstApp.offset,
    .imemBytes = sec2Booter.firstApp.size,
    .dmemPhysicalBase = 0u,
    .dmemVirtualBase = 0u,
    .dmemBytes = sec2Booter.load.osDataSize,
    .pkcOffset = 0x10u,
    .engineIdMask = 1u,
    .ucodeId = 3u,
    .mailbox = addresses.wprMetadata,
  });
  if (!sec2Exec.valid) return {};
  PhasePlan execSec2{BootPhase::ExecuteSec2Booter};
  AppendActions(execSec2, sec2Exec);
  out.phases.push_back(std::move(execSec2));

  PhasePlan verifySec2{BootPhase::VerifySec2Booter};
  verifySec2.checks.push_back({CheckKind::MmioMaskEqual, kSec2Mailbox0, 0xFFFFFFFFu, 0u});
  out.phases.push_back(std::move(verifySec2));

  PhasePlan release{BootPhase::ReleaseGspRiscv};
  release.actions.push_back(Write32(kGspFalconOs, 0u));
  out.phases.push_back(std::move(release));

  PhasePlan verifyGsp{BootPhase::VerifyGspRiscv};
  verifyGsp.checks.push_back({CheckKind::MmioMaskEqual, kGspRiscvCpuCtl, kRiscvActiveMask, kRiscvActiveMask});
  out.phases.push_back(std::move(verifyGsp));

  PhasePlan statusQ{BootPhase::WaitStatusQueue};
  statusQ.checks.push_back({CheckKind::SharedMemoryU32Equal,
      manifest.queues.statusQueueOffset + kStatusQueueEntryOffField, 0xFFFFFFFFu, kExpectedQueueEntryOff});
  out.phases.push_back(std::move(statusQ));

  out.valid = true;
  out.executableWithCurrentCore = manifest.bootstrapRpcPrefillImplemented;
  return out;
}

} // namespace rtxmac::nvidia::gsp
