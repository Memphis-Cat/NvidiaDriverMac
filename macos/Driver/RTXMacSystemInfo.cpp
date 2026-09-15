#include "RTXMacSystemInfo.hpp"

namespace {
constexpr std::uint32_t kPciBar0Offset = 0x10u;
constexpr std::uint32_t kPciDeviceVendorOffset = 0x00u;
constexpr std::uint32_t kPciSubsystemOffset = 0x2Cu;
constexpr std::uint32_t kPciRevisionOffset = 0x08u;

kern_return_t ReadBar(IOPCIDevice* pci,
                      std::uint8_t barIndex,
                      RTXMacSystemBar* out) noexcept {
  if (!pci || !out || barIndex >= 6u) return kIOReturnBadArgument;

  RTXMacSystemBar result{};
  result.barIndex = barIndex;
  kern_return_t kr = pci->GetBARInfo(
      barIndex, &result.memoryIndex, &result.size, &result.driverKitType);
  if (kr != kIOReturnSuccess || result.size == 0u) {
    return kr == kIOReturnSuccess ? kIOReturnNoResources : kr;
  }

  const std::uint32_t offset = kPciBar0Offset +
      static_cast<std::uint32_t>(barIndex) * sizeof(std::uint32_t);
  std::uint32_t low = 0u;
  kr = pci->ConfigurationRead32(offset, &low);
  if (kr != kIOReturnSuccess) return kr;

  const bool is64Bit = (low & 0x7u) == 0x4u;
  std::uint32_t high = 0u;
  bool highAvailable = false;
  if (is64Bit) {
    if (barIndex == 5u) return kIOReturnUnsupported;
    kr = pci->ConfigurationRead32(offset + sizeof(std::uint32_t), &high);
    if (kr != kIOReturnSuccess) return kr;
    highAvailable = true;
  }

  result.decoded = rtxmac::pci::DecodeMemoryBar(
      barIndex, low, high, highAvailable, result.size);
  if (result.decoded.status != rtxmac::pci::BarStatus::Ok) {
    return kIOReturnUnsupported;
  }

  *out = result;
  return kIOReturnSuccess;
}
} // namespace

kern_return_t RTXMacCollectSystemInfo(
    IOPCIDevice* pci,
    RTXMacSystemInfoSnapshot* out) noexcept {
  if (!pci || !out) return kIOReturnBadArgument;
  *out = {};

  constexpr std::array<std::uint8_t, 3u> kRequiredBars{0u, 1u, 3u};
  for (std::size_t i = 0u; i < kRequiredBars.size(); ++i) {
    const kern_return_t kr = ReadBar(pci, kRequiredBars[i], &out->bars[i]);
    if (kr != kIOReturnSuccess) return kr;
  }

  kern_return_t kr = pci->GetBusDeviceFunction(
      &out->bus, &out->device, &out->function);
  if (kr != kIOReturnSuccess || out->device > 31u || out->function > 7u) {
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }

  std::uint32_t deviceVendor = 0u;
  std::uint32_t subsystem = 0u;
  std::uint8_t revision = 0u;
  kr = pci->ConfigurationRead32(kPciDeviceVendorOffset, &deviceVendor);
  if (kr != kIOReturnSuccess) return kr;
  kr = pci->ConfigurationRead32(kPciSubsystemOffset, &subsystem);
  if (kr != kIOReturnSuccess) return kr;
  kr = pci->ConfigurationRead8(kPciRevisionOffset, &revision);
  if (kr != kIOReturnSuccess) return kr;

  const std::uint64_t bdf =
      (static_cast<std::uint64_t>(out->bus) << 8u) |
      (static_cast<std::uint64_t>(out->device) << 3u) |
      static_cast<std::uint64_t>(out->function);

  out->inputs = {
      .bar0Physical = out->bars[0].decoded.base,
      .bar1Physical = out->bars[1].decoded.base,
      .bar3Physical = out->bars[2].decoded.base,
      .domainBusDeviceFunction = bdf,
      .maxUserVa = 0x7FFFFFFFF000ull,
      .pciConfigMirrorBase = 0x88000u,
      .pciConfigMirrorSize = 0x1000u,
      .pciDeviceIdDword = deviceVendor,
      .pciSubDeviceIdDword = subsystem,
      .pciRevisionId = revision,
      .passthrough = true,
  };
  return kIOReturnSuccess;
}
