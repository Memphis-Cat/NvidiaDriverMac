#include "RTXMacReset.hpp"

#include <PCIDriverKit/PCIDriverKit.h>

namespace {
constexpr std::uint64_t kPcieDeviceCapabilitiesOffset = 0x04ull;
constexpr std::uint32_t kPcieDeviceCapabilitiesFlr = 0x10000000u; // bit 28
} // namespace

kern_return_t RTXMacQueryResetCapabilities(
    IOPCIDevice* pci,
    bool hotResetAllowed,
    rtxmac::nvidia::gsp::ResetCapabilities* capabilities,
    std::uint64_t* pcieCapabilityOffset,
    std::uint32_t* pcieDeviceCapabilities) noexcept {
  if (!pci || !capabilities) return kIOReturnBadArgument;
  *capabilities = {};

  std::uint64_t capabilityOffset = 0u;
  kern_return_t kr = pci->FindPCICapability(
      kIOPCICapabilityIDPCIExpress, 0u, &capabilityOffset);
  if (kr != kIOReturnSuccess || capabilityOffset == 0u) {
    return kr == kIOReturnSuccess ? kIOReturnNotFound : kr;
  }

  std::uint32_t deviceCapabilities = 0u;
  pci->ConfigurationRead32(
      capabilityOffset + kPcieDeviceCapabilitiesOffset, &deviceCapabilities);
  if (deviceCapabilities == 0xFFFFFFFFu) return kIOReturnIOError;

  capabilities->functionLevelResetSupported =
      (deviceCapabilities & kPcieDeviceCapabilitiesFlr) != 0u;
  capabilities->hotResetAllowed = hotResetAllowed;

  if (pcieCapabilityOffset) *pcieCapabilityOffset = capabilityOffset;
  if (pcieDeviceCapabilities) *pcieDeviceCapabilities = deviceCapabilities;
  return kIOReturnSuccess;
}

RTXMacResetExecutionResult RTXMacExecuteRecoveryReset(
    IOPCIDevice* pci,
    const rtxmac::nvidia::gsp::GpuResetPlan& plan,
    bool resetsEnabled,
    bool hotResetAllowed) noexcept {
  using namespace rtxmac::nvidia::gsp;
  RTXMacResetExecutionResult out{};
  out.status = kIOReturnNotPermitted;

  if (!resetsEnabled) return out;
  if (!pci || !plan.valid || !ResetPlanInvalidatesBootState(plan) ||
      plan.failure != ResetPlanFailure::None ||
      plan.mechanism == PciResetMechanism::None) {
    out.status = kIOReturnBadArgument;
    return out;
  }

  IOOptionBits resetType = 0u;
  if (plan.mechanism == PciResetMechanism::FunctionLevelReset) {
    ResetCapabilities liveCapabilities{};
    const kern_return_t capKr = RTXMacQueryResetCapabilities(
        pci, hotResetAllowed, &liveCapabilities, nullptr, nullptr);
    if (capKr != kIOReturnSuccess) {
      out.status = capKr;
      return out;
    }
    if (!liveCapabilities.functionLevelResetSupported) {
      out.status = kIOReturnUnsupported;
      return out;
    }
    resetType = kIOPCIDeviceResetTypeFunctionReset;
  } else if (plan.mechanism == PciResetMechanism::HotReset) {
    // Hot reset toggles Secondary Bus Reset in the upstream bridge and may
    // affect sibling functions/devices. Require a second live permission even
    // if the portable plan selected it earlier.
    if (!hotResetAllowed) {
      out.status = kIOReturnNotPermitted;
      return out;
    }
    resetType = kIOPCIDeviceResetTypeHotReset;
  } else {
    out.status = kIOReturnUnsupported;
    return out;
  }

  const kern_return_t kr = pci->Reset(resetType, 0u);
  out.status = kr;
  if (kr == kIOReturnSuccess) {
    out.resetIssued = true;
    out.coldRebuildRequired = true;
  }
  return out;
}
