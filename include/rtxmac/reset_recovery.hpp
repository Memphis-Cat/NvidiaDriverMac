#pragma once

#include <cstdint>

namespace rtxmac::nvidia::gsp {

enum class PciResetMechanism : std::uint8_t {
  None = 0,
  FunctionLevelReset,
  HotReset,
};

enum class ResetPlanFailure : std::uint8_t {
  None = 0,
  ExecutionGateDisabled,
  NoSupportedReset,
};

struct ResetCapabilities {
  // PCIe Device Capabilities bit 28 (PCI_EXP_DEVCAP_FLR).
  bool functionLevelResetSupported{};
  // Hot reset affects the downstream bus through the upstream bridge. Keep this
  // as an explicit policy permission instead of assuming it is always safe.
  bool hotResetAllowed{};
};

struct GpuResetPlan {
  bool valid{};
  PciResetMechanism mechanism{PciResetMechanism::None};
  ResetPlanFailure failure{ResetPlanFailure::None};

  // A successful GPU reset invalidates all boot-time mappings/artifacts. These
  // flags are deliberately explicit so no caller can treat reset as a simple
  // continuation of the failed bootstrap attempt.
  bool invalidateSystemDma{};
  bool invalidateFramebufferStaging{};
  bool invalidateSysmemFlushProgramming{};
  bool invalidatePciCommandProgramming{};
  bool invalidateFalconPlans{};
  bool requireWpr2DownBeforeRebuild{};
  bool requireCompleteColdRebuild{};
};

// Select the least disruptive supported reset. FLR is preferred because it is
// function-scoped. Hot reset is only selected when FLR is unavailable and the
// caller explicitly permits resetting the downstream bus.
[[nodiscard]] GpuResetPlan PlanGpuRecoveryReset(
    const ResetCapabilities& capabilities,
    bool executionGateEnabled = false) noexcept;

[[nodiscard]] constexpr bool ResetPlanInvalidatesBootState(
    const GpuResetPlan& plan) noexcept {
  return plan.valid &&
         plan.invalidateSystemDma &&
         plan.invalidateFramebufferStaging &&
         plan.invalidateSysmemFlushProgramming &&
         plan.invalidatePciCommandProgramming &&
         plan.invalidateFalconPlans &&
         plan.requireWpr2DownBeforeRebuild &&
         plan.requireCompleteColdRebuild;
}

} // namespace rtxmac::nvidia::gsp
