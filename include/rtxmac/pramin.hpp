#pragma once

#include <cstdint>
#include <vector>

namespace rtxmac::nvidia {

// GA102-class Nouveau still uses the legacy PRAMIN path:
//   BAR0 + 0x001700 selects a 1 MiB VRAM window (value = windowBase >> 16)
//   BAR0 + 0x700000 exposes that selected window to the CPU.
//
// This module is deliberately planning-only. It never performs MMIO writes.
inline constexpr std::uint64_t kPraminWindowBytes = 0x100000ull;
inline constexpr std::uint64_t kPraminWindowMask = kPraminWindowBytes - 1ull;
inline constexpr std::uint32_t kPraminWindowSelectOffset = 0x001700u;
inline constexpr std::uint32_t kPraminApertureOffset = 0x700000u;

struct PraminStageChunk {
  // GPU framebuffer/VRAM byte offset represented by the first byte in this chunk.
  std::uint64_t vramOffset{};
  // Offset into the caller's source image.
  std::uint64_t sourceOffset{};
  // Bytes that can be copied without crossing the selected 1 MiB PRAMIN window.
  std::uint64_t bytes{};
  // 1 MiB-aligned VRAM base selected through BAR0+0x1700.
  std::uint64_t windowBase{};
  // 32-bit register value written to BAR0+0x1700 when execution is eventually enabled.
  std::uint32_t windowSelector{};
  // BAR0 byte offset where this chunk begins after selecting windowBase.
  std::uint32_t bar0ApertureOffset{};
};

struct PraminStagePlan {
  bool valid{};
  std::uint64_t vramSize{};
  std::uint64_t vramOffset{};
  std::uint64_t totalBytes{};
  std::vector<PraminStageChunk> chunks;
};

// Split a framebuffer write into chunks that never cross a GA102 PRAMIN window.
// The requested [vramOffset, vramOffset + byteCount) range must be entirely
// inside VRAM. This only computes addresses/selectors; it does not touch BAR0.
[[nodiscard]] PraminStagePlan PlanPraminStage(
    std::uint64_t vramOffset,
    std::uint64_t byteCount,
    std::uint64_t vramSize) noexcept;

} // namespace rtxmac::nvidia
