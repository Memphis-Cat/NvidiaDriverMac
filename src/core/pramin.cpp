#include "rtxmac/pramin.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace rtxmac::nvidia {

PraminStagePlan PlanPraminStage(
    std::uint64_t vramOffset,
    std::uint64_t byteCount,
    std::uint64_t vramSize) noexcept {
  PraminStagePlan out{
      .valid = false,
      .vramSize = vramSize,
      .vramOffset = vramOffset,
      .totalBytes = byteCount,
      .chunks = {},
  };

  if (vramSize == 0u || byteCount == 0u || vramOffset >= vramSize) return out;
  if (byteCount > vramSize - vramOffset) return out;

  std::uint64_t remaining = byteCount;
  std::uint64_t currentVram = vramOffset;
  std::uint64_t sourceOffset = 0u;

  while (remaining != 0u) {
    const std::uint64_t windowBase = currentVram & ~kPraminWindowMask;
    const std::uint64_t inWindow = currentVram & kPraminWindowMask;
    const std::uint64_t room = kPraminWindowBytes - inWindow;
    const std::uint64_t chunkBytes = std::min(remaining, room);

    // Current GA102-sized framebuffer offsets fit comfortably, but keep the
    // serialized MMIO fields strict rather than silently truncating.
    const std::uint64_t selector64 = windowBase >> 16u;
    const std::uint64_t aperture64 =
        static_cast<std::uint64_t>(kPraminApertureOffset) + inWindow;
    if (selector64 > std::numeric_limits<std::uint32_t>::max() ||
        aperture64 > std::numeric_limits<std::uint32_t>::max()) {
      out.chunks.clear();
      return out;
    }

    out.chunks.push_back(PraminStageChunk{
        .vramOffset = currentVram,
        .sourceOffset = sourceOffset,
        .bytes = chunkBytes,
        .windowBase = windowBase,
        .windowSelector = static_cast<std::uint32_t>(selector64),
        .bar0ApertureOffset = static_cast<std::uint32_t>(aperture64),
    });

    // byteCount <= vramSize-vramOffset proved these additions cannot overflow.
    currentVram += chunkBytes;
    sourceOffset += chunkBytes;
    remaining -= chunkBytes;
  }

  out.valid = !out.chunks.empty() && sourceOffset == byteCount;
  return out;
}

PraminDryRunMmioPlan BuildPraminDryRunMmioPlan(
    const PraminStagePlan& stage) noexcept {
  PraminDryRunMmioPlan out{};
  if (!stage.valid || stage.vramSize == 0u || stage.totalBytes == 0u ||
      stage.chunks.empty() || stage.vramOffset >= stage.vramSize ||
      stage.totalBytes > stage.vramSize - stage.vramOffset) {
    return out;
  }

  constexpr std::array selectorRules{
      rtxmac::MmioWriteRule{
          .offset = kPraminWindowSelectOffset,
          .writableMask = 0xFFFFFFFFu,
      },
  };
  constexpr std::array apertureRules{
      rtxmac::MmioWriteRegionRule{
          .offset = kPraminApertureOffset,
          .length = kPraminApertureBytes,
          .alignment = 4u,
      },
  };

  std::uint64_t expectedVram = stage.vramOffset;
  std::uint64_t expectedSource = 0u;
  for (const auto& chunk : stage.chunks) {
    if (chunk.bytes == 0u || chunk.vramOffset != expectedVram ||
        chunk.sourceOffset != expectedSource || (chunk.bytes % 4u) != 0u) {
      out.steps.clear();
      return out;
    }

    const std::uint64_t expectedWindow = chunk.vramOffset & ~kPraminWindowMask;
    const std::uint64_t inWindow = chunk.vramOffset & kPraminWindowMask;
    const std::uint64_t room = kPraminWindowBytes - inWindow;
    const std::uint64_t expectedSelector64 = expectedWindow >> 16u;
    const std::uint64_t expectedAperture64 =
        static_cast<std::uint64_t>(kPraminApertureOffset) + inWindow;
    if (chunk.bytes > room ||
        expectedSelector64 > std::numeric_limits<std::uint32_t>::max() ||
        expectedAperture64 > std::numeric_limits<std::uint32_t>::max() ||
        chunk.windowBase != expectedWindow ||
        chunk.windowSelector != static_cast<std::uint32_t>(expectedSelector64) ||
        chunk.bar0ApertureOffset != static_cast<std::uint32_t>(expectedAperture64)) {
      out.steps.clear();
      return out;
    }

    const auto selectorDecision = rtxmac::CheckMmioWrite(
        selectorRules, kPraminWindowSelectOffset, 0u, chunk.windowSelector);
    const bool selectorAllowed = selectorDecision == rtxmac::WriteDecision::Allowed;
    out.steps.push_back(PraminMmioStep{
        .kind = PraminMmioStepKind::SelectWindow,
        .bar0Offset = kPraminWindowSelectOffset,
        .bytes = 4u,
        .value = chunk.windowSelector,
        .allowed = selectorAllowed,
    });

    const auto apertureDecision = rtxmac::CheckMmioRegionWrite(
        apertureRules, chunk.bar0ApertureOffset, chunk.bytes);
    const bool apertureAllowed = apertureDecision == rtxmac::RegionWriteDecision::Allowed;
    out.steps.push_back(PraminMmioStep{
        .kind = PraminMmioStepKind::WriteApertureRegion,
        .bar0Offset = chunk.bar0ApertureOffset,
        .bytes = chunk.bytes,
        .value = 0u,
        .allowed = apertureAllowed,
    });

    if (!selectorAllowed || !apertureAllowed) {
      out.valid = false;
      return out;
    }

    expectedVram += chunk.bytes;
    expectedSource += chunk.bytes;
  }

  out.valid = expectedSource == stage.totalBytes &&
      expectedVram == stage.vramOffset + stage.totalBytes;
  return out;
}

} // namespace rtxmac::nvidia
