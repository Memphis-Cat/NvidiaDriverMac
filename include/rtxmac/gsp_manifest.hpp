#pragma once

#include "rtxmac/dma.hpp"
#include "rtxmac/falcon_plan.hpp"
#include "rtxmac/gsp_boot.hpp"
#include "rtxmac/gsp_bootstrap.hpp"
#include "rtxmac/gsp_metadata.hpp"
#include "rtxmac/gsp_radix3.hpp"
#include "rtxmac/nvfw.hpp"
#include "rtxmac/pramin.hpp"
#include "rtxmac/vbios.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rtxmac::nvidia::gsp {

enum class MemoryDomain : std::uint8_t { System, Framebuffer };
enum class AllocationKind : std::uint8_t {
  QueueBacking, CachedArguments, LibosInitArguments, WprMetadata,
  Radix3Firmware, FirmwareSignature, GspBootloader, FrtsFwsecImage, Sec2BooterImage,
};

enum class DmaLayoutRequirement : std::uint8_t {
  None = 0,
  Linear,
  PageList,
};

struct AllocationRequirement {
  AllocationKind kind{};
  MemoryDomain domain{MemoryDomain::System};
  std::uint64_t logicalBytes{};
  std::uint64_t allocationBytes{};
  std::uint64_t alignment{};
  bool requiresDmaMapping{};
  DmaLayoutRequirement dmaLayout{DmaLayoutRequirement::None};
};

struct ResolvedDmaAllocation {
  AllocationKind kind{};
  DmaLayoutRequirement layout{DmaLayoutRequirement::None};
  std::uint64_t baseAddress{};
  std::uint64_t allocationBytes{};
  std::vector<std::uint64_t> pageAddresses;
};

[[nodiscard]] std::optional<ResolvedDmaAllocation> ResolveSystemDmaAllocation(
    const AllocationRequirement& requirement,
    std::span<const rtxmac::DmaSegment> segments) noexcept;

struct ManifestInputs {
  std::uint64_t fbSize{}; std::uint64_t vgaWorkspaceOffset{}; std::uint64_t vbiosReservedOffset{}; std::uint64_t wprEndMargin{};
  std::uint64_t frtsSize{0x100000ull}; std::uint64_t nonWprHeapSize{}; std::uint64_t requestedWprHeapSize{};
  std::uint64_t gspFirmwareImageBytes{}; std::uint64_t gspSignatureBytes{}; std::uint64_t gspBootloaderBytes{};
  std::uint64_t frtsFwsecImageBytes{}; std::uint64_t sec2BooterImageBytes{}; std::uint64_t queueBytes{kDefaultQueueBytes};
};
struct BootManifest { bool valid{}; ManifestInputs inputs{}; QueueMemoryLayout queues{}; Radix3Layout radix3{}; WprLayout wpr{}; std::vector<AllocationRequirement> allocations; bool bootstrapRpcPrefillImplemented{}; };
[[nodiscard]] BootManifest PlanBootManifest(const ManifestInputs& in);

struct ResolvedAddresses {
  std::uint64_t queueBacking{}; std::uint64_t cachedArguments{}; std::uint64_t libosInitArguments{}; std::uint64_t wprMetadata{};
  std::uint64_t radix3FirmwareRoot{}; std::uint64_t firmwareSignature{}; std::uint64_t gspBootloader{};
  std::uint64_t frtsFwsecImage{}; std::uint64_t sec2BooterImage{};
};

struct FramebufferStageImage {
  AllocationKind kind{};
  std::uint64_t vramOffset{};
  std::uint64_t logicalBytes{};
  std::uint64_t allocationBytes{};
  rtxmac::nvidia::PraminStagePlan pramin;
};

struct FramebufferStagingPlan {
  bool valid{};
  std::vector<FramebufferStageImage> images;
};

[[nodiscard]] FramebufferStagingPlan PlanFramebufferStaging(
    const BootManifest& manifest,
    const ResolvedAddresses& addresses) noexcept;

struct FramebufferStageArtifact {
  AllocationKind kind{};
  std::uint64_t vramOffset{};
  std::uint64_t logicalBytes{};
  std::uint64_t allocationBytes{};
  rtxmac::nvidia::PraminStagePlan pramin;
  std::vector<std::uint8_t> bytes;
};

[[nodiscard]] std::optional<FramebufferStageArtifact> BuildFramebufferStageArtifact(
    const FramebufferStageImage& plan,
    std::span<const std::uint8_t> firmware) noexcept;

struct ResolvedArtifacts {
  std::array<std::uint8_t, kGspArgumentsCachedBytes> cachedArguments{};
  std::array<std::uint8_t, kWprMetaBytes> wprMetadata{};
  std::array<std::uint8_t, kLibosInitPageBytes> libosInitArguments{};
  std::vector<std::uint8_t> sharedQueueAllocation;
  std::vector<std::uint8_t> radix3FirmwareAllocation;
  std::vector<std::uint8_t> bootstrapCommandQueue;
};
[[nodiscard]] std::optional<ResolvedArtifacts> BuildResolvedArtifacts(
    const BootManifest& manifest, const ResolvedAddresses& addresses, const fw::RiscvBootloaderInfo& bootloader,
    std::span<const std::uint64_t> queueDmaPageAddresses,
    std::span<const std::uint64_t> radix3DmaPageAddresses,
    std::span<const std::uint8_t> gspFirmwareImage,
    std::span<const LibosRegion> libosRegions, const GspSystemInfoInputs& systemInfo,
    std::span<const RegistryDwordEntry> bootstrapRegistry) noexcept;

enum class CheckKind : std::uint8_t { MmioMaskEqual, MmioNonZero, SharedMemoryU32Equal, HostPreparationRequired };
struct Check { CheckKind kind{}; std::uint64_t addressOrOffset{}; std::uint32_t mask{}; std::uint32_t value{}; };
enum class BootPhase : std::uint8_t { PrefillBootstrapRpcRecords, ResetGspForFrts, ExecuteFrtsFwsec, VerifyWpr2, ResetGspForRiscv, ProgramLibosMailbox, ResetSec2, ExecuteSec2Booter, VerifySec2Booter, ReleaseGspRiscv, VerifyGspRiscv, WaitStatusQueue };
struct PhasePlan { BootPhase phase{}; std::vector<falcon::Action> actions; std::vector<Check> checks; };
struct BootSequence { bool valid{}; bool executableWithCurrentCore{}; std::vector<PhasePlan> phases; };

// The parsed GSP RISC-V descriptor is part of the live boot sequence, not only
// artifact construction: NVIDIA programs FALCON_OS with descriptor.appVersion
// after SEC2 resumes GSP-RM and before checking that RISC-V is active.
[[nodiscard]] BootSequence PlanBootSequence(
    const BootManifest& manifest,
    const ResolvedAddresses& addresses,
    const fw::RiscvBootloaderInfo& gspBootloader,
    const vbios::DescriptorV3& fwsec,
    const fw::BooterImageInfo& sec2Booter,
    std::uint32_t chipId);

} // namespace rtxmac::nvidia::gsp
