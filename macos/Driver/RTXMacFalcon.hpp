#pragma once

#include <DriverKit/IOMemoryDescriptor.h>
#include <DriverKit/IOReturn.h>

#include "rtxmac/falcon_policy.hpp"

#include <cstddef>
#include <cstdint>

struct RTXMacFalconExecutionResult {
  kern_return_t status{kIOReturnError};
  std::size_t actionIndex{};
  std::size_t actionsCompleted{};
  std::uint64_t totalWaitMilliseconds{};
  bool hardwareActionsStarted{};
};

// Cold/default-off executor for a policy-validated GA102 Falcon action plan.
// This is deliberately not connected to Start_Impl or the host app. A caller
// must pair any failure after hardwareActionsStarted with the phase-level
// RecoveryForBootFailure() policy; there is no generic register undo here.
[[nodiscard]] RTXMacFalconExecutionResult RTXMacExecuteFalconPlan(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::falcon::Plan& plan,
    bool writesEnabled = false,
    std::uint64_t maxTotalWaitMilliseconds = 120000ull) noexcept;
