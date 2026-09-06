#include "rtxmac/reset_recovery.hpp"

namespace rtxmac::nvidia::gsp {

GpuResetPlan PlanGpuRecoveryReset(
    const ResetCapabilities& capabilities,
    bool executionGateEnabled) noexcept {
  GpuResetPlan out{};
  if (!executionGateEnabled) {
    out.failure = ResetPlanFailure::ExecutionGateDisabled;
    return out;
  }

  if (capabilities.functionLevelResetSupported) {
    out.mechanism = PciResetMechanism::FunctionLevelReset;
  } else if (capabilities.hotResetAllowed) {
    out.mechanism = PciResetMechanism::HotReset;
  } else {
    out.failure = ResetPlanFailure::NoSupportedReset;
    return out;
  }

  out.valid = true;
  out.failure = ResetPlanFailure::None;
  out.invalidateSystemDma = true;
  out.invalidateFramebufferStaging = true;
  out.invalidateSysmemFlushProgramming = true;
  out.invalidatePciCommandProgramming = true;
  out.invalidateFalconPlans = true;
  out.requireWpr2DownBeforeRebuild = true;
  out.requireCompleteColdRebuild = true;
  return out;
}

} // namespace rtxmac::nvidia::gsp
