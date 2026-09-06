#include "rtxmac/falcon_policy.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
  using namespace rtxmac::nvidia::falcon;

  const auto resetGsp = PlanReset(Engine::Gsp, false, 0x174u);
  const auto resetReport = CheckGa102PlanPolicy(resetGsp);
  assert(resetReport.valid);
  assert(!resetReport.requiresStartCpuAliasSupport);

  const auto resetSec2 = PlanReset(Engine::Sec2, false, 0x174u);
  assert(CheckGa102PlanPolicy(resetSec2).valid);

  HsParameters hs{};
  hs.engine = Engine::Gsp;
  hs.imagePhysicalAddress = 0x1F0000000ull;
  hs.codeOffset = 0x1000u;
  hs.dataOffset = 0x3000u;
  hs.imemPhysicalBase = 0u;
  hs.imemVirtualBase = 0u;
  hs.imemBytes = 0x1000u;
  hs.dmemPhysicalBase = 0u;
  hs.dmemVirtualBase = 0u;
  hs.dmemBytes = 0x1000u;
  hs.pkcOffset = 0x100u;
  hs.engineIdMask = 1u;
  hs.ucodeId = 5u;
  const auto authenticated = PlanAuthenticatedExecution(hs);
  assert(authenticated.valid);
  const auto authReport = CheckGa102PlanPolicy(authenticated);
  assert(authReport.valid);
  assert(authReport.requiresStartCpuAliasSupport);

  // A plan cannot authorize a new BAR0 register merely by placing it in Action.
  auto unknown = resetGsp;
  unknown.actions.insert(unknown.actions.begin(),
      {ActionKind::Write32, EngineBase(Engine::Gsp) + 0x500u,
       1u, 0xFFFFFFFFu, 0u});
  const auto unknownReport = CheckGa102PlanPolicy(unknown);
  assert(!unknownReport.valid);
  assert(unknownReport.firstDeniedIndex == 0u);
  assert(unknownReport.firstDenied == ActionPolicyDecision::DeniedUnknownAddress);

  // Nor may a known register use bits outside its independent policy mask.
  Action resetBadMask{ActionKind::MaskedWrite,
      EngineBase(Engine::Gsp) + 0x3C0u, 2u, 2u, 0u};
  assert(CheckGa102ActionPolicy(resetBadMask) == ActionPolicyDecision::DeniedMask);

  // Poll timeouts are bounded; a plan cannot request an effectively unbounded
  // live hardware wait.
  Action longPoll{ActionKind::PollMaskEqual,
      EngineBase(Engine::Gsp) + 0x118u, 0u, 1u, 60001u};
  assert(CheckGa102ActionPolicy(longPoll) == ActionPolicyDecision::DeniedTimeout);

  // Unbalanced conditional control flow is rejected even if individual actions
  // target allowed registers.
  Plan unbalanced{true, {{ActionKind::EndIf, 0u, 0u, 0u, 0u}}};
  assert(!CheckGa102PlanPolicy(unbalanced).valid);

  std::cout << "rtxmac GA102 Falcon static-policy tests passed\n";
  return 0;
}
