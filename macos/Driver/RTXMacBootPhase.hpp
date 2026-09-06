#pragma once

#include <DriverKit/IOMemoryDescriptor.h>
#include <DriverKit/IOReturn.h>

#include "RTXMacDma.hpp"
#include "RTXMacFalcon.hpp"
#include "rtxmac/boot_orchestrator.hpp"

#include <cstddef>
#include <cstdint>

struct RTXMacBootPhaseResult {
  kern_return_t status{kIOReturnError};
  std::size_t phaseIndex{};
  rtxmac::nvidia::gsp::BootPhase phase{};
  bool hardwareActionsStarted{};
  bool checkPassed{};
  bool attemptUpdated{};
  std::uint64_t waitMilliseconds{};
};

// Execute exactly attempt.nextPhaseIndex. The complete sequence is re-audited
// with independent dynamic expectations before any access. This entry point is
// cold/default-off and is not called by Start_Impl or the host application.
//
// sharedQueueDma is required only by WaitStatusQueue; the read is synchronized
// through IODMACommand::PerformOperation rather than assuming the original host
// mapping is coherent with the active device DMA mapping.
[[nodiscard]] RTXMacBootPhaseResult RTXMacExecuteNextBootPhase(
    rtxmac::nvidia::gsp::BootAttempt& attempt,
    const rtxmac::nvidia::gsp::BootManifest& manifest,
    const rtxmac::nvidia::gsp::BootSequence& sequence,
    const rtxmac::nvidia::gsp::BootSequencePolicyExpectations& expectations,
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const RTXMacPreparedDmaBuffer* sharedQueueDma,
    bool writesEnabled = false,
    std::uint64_t maxPhaseWaitMilliseconds = 60000ull) noexcept;
