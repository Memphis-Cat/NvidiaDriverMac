#pragma once

#include <DriverKit/IOService.h>
#include <DriverKit/IOReturn.h>
#include <PCIDriverKit/PCIDriverKit.h>

#include "rtxmac/ga10x_live_preflight.hpp"
#include "rtxmac/ga10x_prototype.hpp"

struct RTXMacLiveBoundaryPreflight {
  kern_return_t ioStatus{kIOReturnError};
  bool captured{};
  rtxmac::nvidia::prototype::ReservedBoundaryDecision decision{};
};

// Read only the BAR0 page containing GA10x MMU-lock state and evaluate the
// portable reserved-boundary policy. This performs no MMIO writes, reset,
// PRAMIN access, Falcon execution, or GSP start.
[[nodiscard]] RTXMacLiveBoundaryPreflight RTXMacCheckLiveReservedBoundary(
    IOService* owner,
    IOPCIDevice* pci,
    const rtxmac::nvidia::prototype::Profile& profile) noexcept;
