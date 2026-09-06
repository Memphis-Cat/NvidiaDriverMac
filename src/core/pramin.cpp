#include "rtxmac/pramin.hpp"

#include <algorithm>
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

} // namespace rtxmac::nvidia
