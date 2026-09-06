#include "rtxmac/boot_recovery.hpp"

namespace rtxmac::nvidia::gsp {
namespace {
constexpr BootFailureRecovery ColdReversible() noexcept {
  return BootFailureRecovery{
      .fullGpuResetRequired = false,
      .mayRetryBootstrap = true,
      .mayRollbackPraminScratch = true,
      .mayRollbackPciCommand = true,
      .mayRollbackSysmemFlushPage = true,
      .mayReleaseDmaBuffers = true,
      .keepDmaAndFlushPinned = false,
  };
}

constexpr BootFailureRecovery FullResetRequired() noexcept {
  return BootFailureRecovery{
      .fullGpuResetRequired = true,
      .mayRetryBootstrap = false,
      .mayRollbackPraminScratch = false,
      .mayRollbackPciCommand = false,
      .mayRollbackSysmemFlushPage = false,
      .mayReleaseDmaBuffers = false,
      .keepDmaAndFlushPinned = true,
  };
}
} // namespace

BootFailureRecovery RecoveryForBootFailure(
    BootPhase failedPhase,
    bool phaseActionsStarted) noexcept {
  switch (failedPhase) {
    case BootPhase::PrefillBootstrapRpcRecords:
      return ColdReversible();

    case BootPhase::ResetGspForFrts:
      // This is the first live hardware reset. Validation/preparation failure
      // before the first reset write is still cold; once reset starts, the GPU
      // may have consumed sysmem/DMA state and must be treated as reset-required.
      return phaseActionsStarted ? FullResetRequired() : ColdReversible();

    case BootPhase::ExecuteFrtsFwsec:
    case BootPhase::VerifyWpr2:
    case BootPhase::ResetGspForRiscv:
    case BootPhase::ProgramLibosMailbox:
    case BootPhase::ResetSec2:
    case BootPhase::ExecuteSec2Booter:
    case BootPhase::VerifySec2Booter:
    case BootPhase::ReleaseGspRiscv:
    case BootPhase::VerifyGspRiscv:
    case BootPhase::WaitStatusQueue:
      // Reaching any of these means the initial GSP reset has already executed.
      // In addition, FRTS may have established WPR2/protected state, which the
      // reference driver refuses to bootstrap over. Do not attempt register
      // undo or free DMA backing while an engine may still reference it.
      return FullResetRequired();
  }

  return FullResetRequired();
}

} // namespace rtxmac::nvidia::gsp
