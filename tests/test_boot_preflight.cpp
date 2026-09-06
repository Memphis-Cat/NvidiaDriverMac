#include "rtxmac/boot_preflight.hpp"

#include <cassert>
#include <iostream>

int main() {
  using namespace rtxmac::nvidia::gsp;

  BootCommitPrerequisites ready{
      .executionGateEnabled = true,
      .pciMemorySpaceEnabled = true,
      .pciBusMasterEnabled = true,
      .sysmemFlushPageProgrammed = true,
      .allSystemDmaResolved = true,
      .resolvedArtifactsBuilt = true,
      .frtsFramebufferImageVerified = true,
      .sec2FramebufferImageVerified = true,
      .bootstrapRpcPrefilled = true,
      .falconPlansPolicyValid = true,
      .wpr2AddrHi = 0u,
  };

  const auto ok = CheckBootCommitPreflight(ready);
  assert(ok.ready);
  assert(ok.firstFailure == BootPreflightFailure::None);
  assert(ok.coldRollbackStillSafe);

  auto gateOff = ready;
  gateOff.executionGateEnabled = false;
  assert(CheckBootCommitPreflight(gateOff).firstFailure ==
         BootPreflightFailure::ExecutionGateDisabled);

  auto noBusMaster = ready;
  noBusMaster.pciBusMasterEnabled = false;
  assert(CheckBootCommitPreflight(noBusMaster).firstFailure ==
         BootPreflightFailure::PciCommandNotReady);

  auto noFlush = ready;
  noFlush.sysmemFlushPageProgrammed = false;
  assert(CheckBootCommitPreflight(noFlush).firstFailure ==
         BootPreflightFailure::SysmemFlushPageNotReady);

  auto missingFrts = ready;
  missingFrts.frtsFramebufferImageVerified = false;
  assert(CheckBootCommitPreflight(missingFrts).firstFailure ==
         BootPreflightFailure::FrtsImageNotVerified);

  auto staleWpr = ready;
  staleWpr.wpr2AddrHi = 1u;
  const auto staleReport = CheckBootCommitPreflight(staleWpr);
  assert(!staleReport.ready);
  assert(staleReport.firstFailure == BootPreflightFailure::Wpr2AlreadyActive);
  // The preflight itself never starts ResetGspForFrts, so failure here remains
  // cold/reversible even when it detects a GPU state that must not be booted.
  assert(staleReport.coldRollbackStillSafe);

  std::cout << "rtxmac GSP hardware-commit preflight tests passed\n";
  return 0;
}
