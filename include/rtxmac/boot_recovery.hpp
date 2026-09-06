#pragma once

#include "rtxmac/gsp_manifest.hpp"

namespace rtxmac::nvidia::gsp {

// Recovery is intentionally conservative. The first live GSP Falcon reset is
// the hardware commit boundary: before it starts, cold resources may be undone;
// after it starts, we do not attempt an in-place replay of the GSP bootstrap.
struct BootFailureRecovery {
  bool fullGpuResetRequired{};
  bool mayRetryBootstrap{};
  bool mayRollbackPraminScratch{};
  bool mayRollbackPciCommand{};
  bool mayRollbackSysmemFlushPage{};
  bool mayReleaseDmaBuffers{};
  bool keepDmaAndFlushPinned{};
};

// phaseActionsStarted means the phase has crossed from validation/checking into
// actual hardware actions. It matters for ResetGspForFrts: a failure before the
// first reset write is still cold/reversible, while any failure after that write
// crosses the commit boundary.
[[nodiscard]] BootFailureRecovery RecoveryForBootFailure(
    BootPhase failedPhase,
    bool phaseActionsStarted) noexcept;

[[nodiscard]] constexpr bool IsFullResetRecoveryConsistent(
    const BootFailureRecovery& recovery) noexcept {
  if (!recovery.fullGpuResetRequired) return true;
  return !recovery.mayRetryBootstrap &&
         !recovery.mayRollbackPraminScratch &&
         !recovery.mayRollbackPciCommand &&
         !recovery.mayRollbackSysmemFlushPage &&
         !recovery.mayReleaseDmaBuffers &&
         recovery.keepDmaAndFlushPinned;
}

} // namespace rtxmac::nvidia::gsp
