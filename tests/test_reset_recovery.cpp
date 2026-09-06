#include "rtxmac/reset_recovery.hpp"

#include <cassert>
#include <iostream>

int main() {
  using namespace rtxmac::nvidia::gsp;

  // Reset planning is inert unless the caller crosses a separate explicit gate.
  const auto gated = PlanGpuRecoveryReset({true, true});
  assert(!gated.valid);
  assert(gated.failure == ResetPlanFailure::ExecutionGateDisabled);

  const auto flr = PlanGpuRecoveryReset({true, true}, true);
  assert(flr.valid);
  assert(flr.mechanism == PciResetMechanism::FunctionLevelReset);
  assert(ResetPlanInvalidatesBootState(flr));

  // Hot reset is fallback only and must have an explicit policy permission.
  const auto hot = PlanGpuRecoveryReset({false, true}, true);
  assert(hot.valid);
  assert(hot.mechanism == PciResetMechanism::HotReset);
  assert(ResetPlanInvalidatesBootState(hot));

  const auto none = PlanGpuRecoveryReset({false, false}, true);
  assert(!none.valid);
  assert(none.mechanism == PciResetMechanism::None);
  assert(none.failure == ResetPlanFailure::NoSupportedReset);

  std::cout << "rtxmac conservative PCI GPU reset recovery tests passed\n";
  return 0;
}
