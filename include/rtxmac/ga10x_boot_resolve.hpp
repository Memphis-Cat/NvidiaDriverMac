#pragma once

#include "rtxmac/ga10x_boot_prepare.hpp"
#include "rtxmac/package_dma_resolve.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace rtxmac::nvidia::prototype {

struct GeneratedPhysicalView {
  GeneratedBufferKind kind{};
  std::span<const std::uint64_t> pageAddresses{};
};

enum class BootResolveStatus : std::uint8_t {
  Ok = 0,
  BadProfile,
  BadPreparationPlan,
  BadStagedPackageSummary,
  WrongGeneratedCount,
  GeneratedKindMismatch,
  GeneratedPageCountMismatch,
  GeneratedPageAddressInvalid,
  GeneratedLinearLayoutRejected,
  PackageReuseMissing,
  PackageReuseMismatch,
};

struct ResolvedBootMemory {
  BootResolveStatus status{BootResolveStatus::BadProfile};
  gsp::ResolvedAddresses addresses{};
  std::array<gsp::LibosRegion, 6u> libosRegions{};
  std::uint64_t generatedTotalPages{};
};

// Resolve the final cold boot address graph from generated DMA page lists plus
// the already-staged package summary. This is non-owning and performs no
// allocation or hardware access. Only the package's linear GSP signature and
// GSP bootloader are reused; the raw staged .fwimage is intentionally ignored
// because final boot uses a separately allocated Radix3 image.
[[nodiscard]] ResolvedBootMemory ResolveGa10xBootMemory(
    const Profile& profile,
    const BootPreparationPlan& plan,
    const package::ResolvedPackageDmaSummary& stagedPackage,
    std::span<const GeneratedPhysicalView> generated) noexcept;

[[nodiscard]] const char* BootResolveStatusName(BootResolveStatus status) noexcept;

} // namespace rtxmac::nvidia::prototype
