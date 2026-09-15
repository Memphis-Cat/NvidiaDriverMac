#include "rtxmac/pci_bar.hpp"

namespace rtxmac::pci {

DecodedBar DecodeMemoryBar(std::uint8_t barIndex,
                           std::uint32_t lowDword,
                           std::uint32_t highDword,
                           bool highDwordAvailable,
                           std::uint64_t expectedSize) noexcept {
  DecodedBar out{};
  out.barIndex = barIndex;
  if (barIndex >= 6u) {
    out.status = BarStatus::InvalidIndex;
    return out;
  }
  if ((lowDword & 0x1u) != 0u) {
    out.status = BarStatus::IoSpaceUnsupported;
    return out;
  }
  if (expectedSize == 0u || (expectedSize & (expectedSize - 1u)) != 0u) {
    out.status = BarStatus::InvalidSize;
    return out;
  }

  const std::uint32_t memoryType = lowDword & 0x6u;
  if (memoryType == 0x2u || memoryType == 0x6u) {
    out.status = BarStatus::ReservedMemoryType;
    return out;
  }

  out.is64Bit = memoryType == 0x4u;
  out.prefetchable = (lowDword & 0x8u) != 0u;
  const std::uint64_t lowBase = static_cast<std::uint64_t>(lowDword & 0xFFFFFFF0u);
  if (out.is64Bit) {
    if (barIndex == 5u || !highDwordAvailable) {
      out.status = BarStatus::MissingHighDword;
      return out;
    }
    out.base = (static_cast<std::uint64_t>(highDword) << 32u) | lowBase;
  } else {
    out.base = lowBase;
  }

  if (out.base == 0u) {
    out.status = BarStatus::ZeroBase;
    return out;
  }
  if ((out.base & (expectedSize - 1u)) != 0u) {
    out.status = BarStatus::BaseSizeMisaligned;
    return out;
  }

  out.size = expectedSize;
  out.status = BarStatus::Ok;
  return out;
}

const char* BarStatusName(BarStatus status) noexcept {
  switch (status) {
    case BarStatus::Ok: return "ok";
    case BarStatus::InvalidIndex: return "invalid-index";
    case BarStatus::IoSpaceUnsupported: return "io-space-unsupported";
    case BarStatus::ReservedMemoryType: return "reserved-memory-type";
    case BarStatus::MissingHighDword: return "missing-high-dword";
    case BarStatus::InvalidSize: return "invalid-size";
    case BarStatus::ZeroBase: return "zero-base";
    case BarStatus::BaseSizeMisaligned: return "base-size-misaligned";
  }
  return "unknown";
}

} // namespace rtxmac::pci
