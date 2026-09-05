#pragma once

#include <cstdint>
#include <string_view>

namespace rtxmac::nvidia {

// Public NVIDIA register definitions. These two registers are read-only
// identification/status registers in NVIDIA's published headers.
inline constexpr std::uint32_t kPmcBoot0Offset = 0x00000000u;
inline constexpr std::uint32_t kPmcBoot42Offset = 0x00000A00u;

struct Boot42Identity {
  std::uint32_t raw{};
  std::uint8_t architecture{};
  std::uint8_t implementation{};
  std::uint8_t majorRevision{};
  std::uint8_t minorRevision{};
  std::uint8_t minorExtendedRevision{};
};

[[nodiscard]] constexpr Boot42Identity DecodeBoot42(std::uint32_t value) noexcept {
  return Boot42Identity{
      .raw = value,
      .architecture = static_cast<std::uint8_t>((value >> 24u) & 0x1Fu),
      .implementation = static_cast<std::uint8_t>((value >> 20u) & 0x0Fu),
      .majorRevision = static_cast<std::uint8_t>((value >> 16u) & 0x0Fu),
      .minorRevision = static_cast<std::uint8_t>((value >> 12u) & 0x0Fu),
      .minorExtendedRevision = static_cast<std::uint8_t>((value >> 8u) & 0x0Fu),
  };
}

[[nodiscard]] constexpr bool IsAmpere(const Boot42Identity& id) noexcept {
  return id.architecture == 0x17u;
}

[[nodiscard]] std::string_view ArchitectureName(std::uint8_t architecture) noexcept;
[[nodiscard]] std::string_view ChipName(const Boot42Identity& id) noexcept;

} // namespace rtxmac::nvidia
