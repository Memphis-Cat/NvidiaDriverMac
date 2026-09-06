#include "rtxmac/gsp_manifest.hpp"

#include <algorithm>
#include <limits>

namespace rtxmac::nvidia::gsp {

std::optional<FramebufferStageArtifact> BuildFramebufferStageArtifact(
    const FramebufferStageImage& plan,
    std::span<const std::uint8_t> firmware) noexcept {
  if (plan.logicalBytes == 0u || plan.allocationBytes == 0u ||
      plan.logicalBytes > plan.allocationBytes ||
      firmware.size() != plan.logicalBytes ||
      !plan.pramin.valid ||
      plan.pramin.vramOffset != plan.vramOffset ||
      plan.pramin.totalBytes != plan.allocationBytes ||
      plan.pramin.vramSize == 0u ||
      plan.vramOffset >= plan.pramin.vramSize ||
      plan.allocationBytes > plan.pramin.vramSize - plan.vramOffset ||
      (plan.vramOffset & 0xFFFu) != 0u ||
      (plan.allocationBytes & 0xFFFu) != 0u ||
      (plan.allocationBytes & 3u) != 0u ||
      plan.allocationBytes > std::numeric_limits<std::size_t>::max()) {
    return std::nullopt;
  }

  FramebufferStageArtifact out{};
  out.kind = plan.kind;
  out.vramOffset = plan.vramOffset;
  out.logicalBytes = plan.logicalBytes;
  out.allocationBytes = plan.allocationBytes;
  out.pramin = plan.pramin;
  out.bytes.assign(static_cast<std::size_t>(plan.allocationBytes), 0u);
  std::copy(firmware.begin(), firmware.end(), out.bytes.begin());

  // assign() zero-initializes the complete allocation; verify that invariant
  // explicitly so this helper remains safe if its construction changes later.
  const auto paddingBegin = out.bytes.begin() + static_cast<std::ptrdiff_t>(plan.logicalBytes);
  if (!std::all_of(paddingBegin, out.bytes.end(), [](std::uint8_t value) { return value == 0u; })) {
    return std::nullopt;
  }
  return out;
}

} // namespace rtxmac::nvidia::gsp
