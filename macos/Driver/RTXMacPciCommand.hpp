#pragma once

#include <DriverKit/IOReturn.h>
#include <PCIDriverKit/PCIDriverKit.h>

#include "rtxmac/pci_command.hpp"

// Apply one prevalidated PCI Command transition. The gate defaults off and the
// helper is intentionally not called by RTXMacDriver::Start_Impl.
[[nodiscard]] kern_return_t RTXMacApplyPciCommandTransition(
    IOPCIDevice* pci,
    const rtxmac::PciCommandTransition& plan,
    bool writesEnabled = false) noexcept;

// Restore the exact command value recorded by a previously valid transition.
// This is a separate explicit operation so higher-level boot orchestration can
// roll back bus mastering after later failures.
[[nodiscard]] kern_return_t RTXMacRollbackPciCommandTransition(
    IOPCIDevice* pci,
    const rtxmac::PciCommandTransition& plan,
    bool writesEnabled = false) noexcept;
