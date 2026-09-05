#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rtxmac {

struct PciIdentity {
  std::uint16_t vendor{};
  std::uint16_t device{};
  std::uint16_t subsystemVendor{};
  std::uint16_t subsystemDevice{};
};

[[nodiscard]] std::optional<PciIdentity> ParseWindowsPciHardwareId(std::string_view text);
[[nodiscard]] bool IsNvidia(const PciIdentity& id) noexcept;
[[nodiscard]] bool IsKnownRtx3060Ti(const PciIdentity& id) noexcept;
[[nodiscard]] std::string Describe(const PciIdentity& id);

} // namespace rtxmac
