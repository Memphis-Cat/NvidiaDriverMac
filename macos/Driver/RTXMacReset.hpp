#pragma once

#include <DriverKit/IOReturn.h>
#include <PCIDriverKit/PCIDriverKit.h>

#include "rtxmac/reset_recovery.hpp"

#include <cstdint>

// Query the endpoint's PCI Express Device Capabilities and construct the small
// portable capability set used by PlanGpuRecoveryReset(). No hardware writes.
[[nodiscard]] kern_return_t RTXMacQueryResetCapabilities(
    IOPCIDevice* pci,
    bool hotResetAllowed,
    rtxmac::nvidia::gsp::ResetCapabilities* capabilities,
    std::uint64_t* pcieCapabilityOffset = nullptr,
    std::uint32_t* pcieDeviceCapabilities = nullptr) noexcept;

struct RTXMacResetExecutionResult {
  kern_return_t status{kIOReturnError};
  bool resetIssued{};
  bool coldRebuildRequired{};
};

// Cold/default-off reset executor. A successful reset is deliberately treated
// as a terminal point for all pre-reset boot state: the caller must discard DMA
// mappings/artifacts/plans and reconstruct the boot pipeline from scratch.
// This function is not called from RTXMacDriver::Start_Impl or the host app.
[[nodiscard]] RTXMacResetExecutionResult RTXMacExecuteRecoveryReset(
    IOPCIDevice* pci,
    const rtxmac::nvidia::gsp::GpuResetPlan& plan,
    bool resetsEnabled = false) noexcept;
