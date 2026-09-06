#include "rtxmac/boot_recovery.hpp"

#include <array>
#include <cassert>
#include <iostream>

int main() {
  using namespace rtxmac::nvidia::gsp;

  const auto hostFailure = RecoveryForBootFailure(
      BootPhase::PrefillBootstrapRpcRecords, true);
  assert(!hostFailure.fullGpuResetRequired);
  assert(hostFailure.mayRetryBootstrap);
  assert(hostFailure.mayRollbackPraminScratch);
  assert(hostFailure.mayRollbackPciCommand);
  assert(hostFailure.mayRollbackSysmemFlushPage);
  assert(hostFailure.mayReleaseDmaBuffers);
  assert(!hostFailure.keepDmaAndFlushPinned);

  const auto resetValidationFailure = RecoveryForBootFailure(
      BootPhase::ResetGspForFrts, false);
  assert(!resetValidationFailure.fullGpuResetRequired);
  assert(resetValidationFailure.mayRetryBootstrap);

  const auto resetStartedFailure = RecoveryForBootFailure(
      BootPhase::ResetGspForFrts, true);
  assert(resetStartedFailure.fullGpuResetRequired);
  assert(IsFullResetRecoveryConsistent(resetStartedFailure));

  constexpr std::array committedPhases{
      BootPhase::ExecuteFrtsFwsec,
      BootPhase::VerifyWpr2,
      BootPhase::ResetGspForRiscv,
      BootPhase::ProgramLibosMailbox,
      BootPhase::ResetSec2,
      BootPhase::ExecuteSec2Booter,
      BootPhase::VerifySec2Booter,
      BootPhase::ReleaseGspRiscv,
      BootPhase::VerifyGspRiscv,
      BootPhase::WaitStatusQueue,
  };

  for (const auto phase : committedPhases) {
    const auto recovery = RecoveryForBootFailure(phase, false);
    assert(recovery.fullGpuResetRequired);
    assert(IsFullResetRecoveryConsistent(recovery));
    assert(recovery.keepDmaAndFlushPinned);
  }

  std::cout << "rtxmac GSP phase-recovery policy tests passed\n";
  return 0;
}
