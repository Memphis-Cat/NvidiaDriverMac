#pragma once

#include "rtxmac/write_policy.hpp"

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
inline constexpr std::uint32_t kPraminApertureBytes = 0x100000u;

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

enum class PraminMmioStepKind : std::uint8_t {
  SelectWindow,
  WriteApertureRegion,
};

struct PraminMmioStep {
  PraminMmioStepKind kind{PraminMmioStepKind::SelectWindow};
  std::uint32_t bar0Offset{};
  std::uint64_t bytes{};
  // Used only by SelectWindow. Data-region contents remain external to the
  // dry-run trace and are never interpreted as register values.
  std::uint32_t value{};
  bool allowed{};
};

struct PraminDryRunMmioPlan {
  bool valid{};
  std::vector<PraminMmioStep> steps;
};

// Convert a staging plan into a policy-checked MMIO trace. The policy grants
// only the exact PRAMIN selector register and the 1 MiB PRAMIN data aperture.
// A malformed/tampered staging plan is rejected. No hardware access occurs.
[[nodiscard]] PraminDryRunMmioPlan BuildPraminDryRunMmioPlan(
    const PraminStagePlan& stage) noexcept;

} // namespace rtxmac::nvidia
