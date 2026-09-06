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

struct ResolvedStartCpuAction {
  bool valid{};
  bool usedAlias{};
  Action action{};
};

// Validate one action against a static GA102 GSP/SEC2 register allowlist. This
// does not trust the plan to authorize itself: addresses and masks are checked
// against constants independent of PlanReset/PlanAuthenticatedExecution.
[[nodiscard]] ActionPolicyDecision CheckGa102ActionPolicy(
    const Action& action) noexcept;

// Resolve StartCpuRespectAlias from a live CPUCTL snapshot. GA102 Falcon
// CPUCTL.alias_en is bit 6. If it is set, start is written as bit 1 (value 0x2)
// to CPUCTL_ALIAS at +0x130; otherwise CPUCTL.startcpu bit 1 is set at +0x100.
// The returned concrete action is itself checked by the static write policy.
[[nodiscard]] ResolvedStartCpuAction ResolveGa102StartCpuAction(
    const Action& action,
    std::uint32_t cpuCtlValue) noexcept;

// Validate control-flow balance in addition to each action. The current plans
// use at most one non-nested IfMaskEqualBegin/EndIf region. CPU start is
// separately flagged because a live CPUCTL read is required before it can be
// resolved into a concrete, policy-checked write.
[[nodiscard]] PlanPolicyReport CheckGa102PlanPolicy(const Plan& plan) noexcept;

} // namespace rtxmac::nvidia::falcon
