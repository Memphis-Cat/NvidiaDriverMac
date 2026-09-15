#pragma once

#include <cstdint>

namespace rtxmac::pci {

enum class BarStatus : std::uint8_t {
  Ok = 0,
  InvalidIndex,
  IoSpaceUnsupported,
  ReservedMemoryType,
  MissingHighDword,
  InvalidSize,
  ZeroBase,
  BaseSizeMisaligned,
};

struct DecodedBar {
  BarStatus status{BarStatus::InvalidIndex};
  std::uint8_t barIndex{};
  std::uint64_t base{};
  std::uint64_t size{};
  bool is64Bit{};
  bool prefetchable{};
};

// Decode one PCI memory BAR from configuration-space dwords without probing or
// writing the BAR. expectedSize comes from PCIDriverKit::GetBARInfo(), so the
// decoded base is also checked against the already-discovered resource size.
[[nodiscard]] DecodedBar DecodeMemoryBar(
    std::uint8_t barIndex,
    std::uint32_t lowDword,
    std::uint32_t highDword,
    bool highDwordAvailable,
    std::uint64_t expectedSize) noexcept;

[[nodiscard]] const char* BarStatusName(BarStatus status) noexcept;

} // namespace rtxmac::pci
