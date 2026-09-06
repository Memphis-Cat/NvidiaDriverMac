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

  Action deferredStart{};
  bool foundStart = false;
  for (const auto& action : authenticated.actions) {
    if (action.kind == ActionKind::StartCpuRespectAlias) {
      deferredStart = action;
      foundStart = true;
      break;
    }
  }
  assert(foundStart);

  const auto directStart = ResolveGa102StartCpuAction(deferredStart, 0u);
  assert(directStart.valid && !directStart.usedAlias);
  assert(directStart.action.kind == ActionKind::MaskedWrite);
  assert(directStart.action.address == EngineBase(Engine::Gsp) + 0x100u);
  assert(directStart.action.value == 0x2u && directStart.action.mask == 0x2u);
  assert(CheckGa102ActionPolicy(directStart.action) == ActionPolicyDecision::Allowed);

  const auto aliasStart = ResolveGa102StartCpuAction(deferredStart, 1u << 6u);
  assert(aliasStart.valid && aliasStart.usedAlias);
  assert(aliasStart.action.kind == ActionKind::Write32);
  assert(aliasStart.action.address == EngineBase(Engine::Gsp) + 0x130u);
  assert(aliasStart.action.value == 0x2u && aliasStart.action.mask == 0x2u);
  assert(CheckGa102ActionPolicy(aliasStart.action) == ActionPolicyDecision::Allowed);

  // NVIDIA programs FALCON_OS with the parsed RISC-V appVersion. The register
  // is allowed, but the value must still come from the boot descriptor rather
  // than a hardcoded sequence constant.
  Action falconOs{ActionKind::Write32,
      EngineBase(Engine::Gsp) + 0x80u, 0x12345678u, 0xFFFFFFFFu, 0u};
  assert(CheckGa102ActionPolicy(falconOs) == ActionPolicyDecision::Allowed);

  auto unknown = resetGsp;
  unknown.actions.insert(unknown.actions.begin(),
      {ActionKind::Write32, EngineBase(Engine::Gsp) + 0x500u,
       1u, 0xFFFFFFFFu, 0u});
  const auto unknownReport = CheckGa102PlanPolicy(unknown);
  assert(!unknownReport.valid);
  assert(unknownReport.firstDeniedIndex == 0u);
  assert(unknownReport.firstDenied == ActionPolicyDecision::DeniedUnknownAddress);

  Action resetBadMask{ActionKind::MaskedWrite,
      EngineBase(Engine::Gsp) + 0x3C0u, 2u, 2u, 0u};
  assert(CheckGa102ActionPolicy(resetBadMask) == ActionPolicyDecision::DeniedMask);

  Action longPoll{ActionKind::PollMaskEqual,
      EngineBase(Engine::Gsp) + 0x118u, 0u, 1u, 60001u};
  assert(CheckGa102ActionPolicy(longPoll) == ActionPolicyDecision::DeniedTimeout);

  Plan unbalanced{true, {{ActionKind::EndIf, 0u, 0u, 0u, 0u}}};
  assert(!CheckGa102PlanPolicy(unbalanced).valid);

  std::cout << "rtxmac GA102 Falcon static-policy tests passed\n";
  return 0;
}
