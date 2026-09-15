#pragma once

#include "rtxmac/ga10x_prototype.hpp"

#include <array>
#include <cstdint>

namespace rtxmac::nvidia::prototype {

inline constexpr std::uint64_t kBootPreparePageBytes = 0x1000ull;
inline constexpr std::uint64_t kLibosLogRegionBytes = 0x10000ull;
inline constexpr std::size_t kLibosLogRegionCount = 5u;
inline constexpr std::size_t kGeneratedDmaRequirementCount = 10u;

enum class GeneratedBufferKind : std::uint8_t {
  QueueBacking = 0u,
  CachedArguments,
  LibosInitArguments,
  WprMetadata,
  Radix3Firmware,
  LogInit,
  LogIntr,
  LogRm,
  LogMnoc,
  LogKrnl,
};

enum class GeneratedDmaLayout : std::uint8_t {
  PageList = 0u,
  Linear,
};

struct GeneratedDmaRequirement {
  GeneratedBufferKind kind{};
  GeneratedDmaLayout layout{GeneratedDmaLayout::PageList};
  std::uint64_t logicalBytes{};
  std::uint64_t allocationBytes{};
  std::uint64_t pageCount{};
};

struct FramebufferScratchPlan {
  std::uint64_t frtsFwsecOffset{};
  std::uint64_t frtsFwsecBytes{};
  std::uint64_t sec2BooterOffset{};
  std::uint64_t sec2BooterBytes{};
  std::uint64_t frtsRegionOffset{};
  std::uint64_t frtsRegionBytes{};
};

enum class BootPreparationPlanStatus : std::uint8_t {
  Ok = 0,
  InvalidProfile,
  InvalidManifest,
  SizeOverflow,
  FramebufferScratchUnderflow,
  FrtsPlacementMismatch,
};

struct BootPreparationPlan {
  BootPreparationPlanStatus status{BootPreparationPlanStatus::InvalidProfile};
  std::array<GeneratedDmaRequirement, kGeneratedDmaRequirementCount> generated{};
  std::uint64_t totalGeneratedAllocationBytes{};
  std::uint64_t reusedSignatureAllocationBytes{};
  std::uint64_t reusedBootloaderAllocationBytes{};
  FramebufferScratchPlan framebuffer{};
};

// Builds the final cold host-memory/scratch-VRAM allocation plan after a
// verified package has been converted to a GA10x prototype profile. Package
// signature + GSP bootloader are reused from the already staged linear DMA
// buffers. The large raw GSP firmware staging buffer is NOT reused as the
// final Radix3 allocation because the latter also contains three levels of page
// tables and therefore has a different size/content.
[[nodiscard]] BootPreparationPlan PlanGa10xBootPreparation(
    const Profile& profile) noexcept;

[[nodiscard]] const char* BootPreparationPlanStatusName(
    BootPreparationPlanStatus status) noexcept;

} // namespace rtxmac::nvidia::prototype
