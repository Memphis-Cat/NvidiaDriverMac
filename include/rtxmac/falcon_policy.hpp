#pragma once

#include "rtxmac/falcon_plan.hpp"

#include <cstddef>
#include <cstdint>

namespace rtxmac::nvidia::falcon {

enum class ActionPolicyDecision : std::uint8_t {
  Allowed = 0,
  RequiresStartCpuAliasSupport,
  DeniedUnknownAddress,
  DeniedMask,
  DeniedValue,
  DeniedTimeout,
  DeniedControlFlow,
};

struct PlanPolicyReport {
  bool valid{};
  bool requiresStartCpuAliasSupport{};
  std::size_t actionCount{};
  std::size_t firstDeniedIndex{};
  ActionPolicyDecision firstDenied{ActionPolicyDecision::Allowed};
};

// Validate one action against a static GA102 GSP/SEC2 register allowlist. This
// does not trust the plan to authorize itself: addresses and masks are checked
// against constants independent of PlanReset/PlanAuthenticatedExecution.
[[nodiscard]] ActionPolicyDecision CheckGa102ActionPolicy(
    const Action& action) noexcept;

// Validate control-flow balance in addition to each action. The current plans
// use at most one non-nested IfMaskEqualBegin/EndIf region. CPU start remains a
// separately flagged live-executor requirement rather than an automatic allow.
[[nodiscard]] PlanPolicyReport CheckGa102PlanPolicy(const Plan& plan) noexcept;

} // namespace rtxmac::nvidia::falcon
