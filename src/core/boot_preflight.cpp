#include "rtxmac/boot_preflight.hpp"

namespace rtxmac::nvidia::gsp {

BootPreflightReport CheckBootCommitPreflight(
    const BootCommitPrerequisites& p) noexcept {
  BootPreflightReport out{};

  const auto fail = [&](BootPreflightFailure failure) noexcept {
    out.ready = false;
    out.coldRollbackStillSafe = true;
    out.firstFailure = failure;
    return out;
  };

  if (!p.executionGateEnabled) return fail(BootPreflightFailure::ExecutionGateDisabled);
  if (!p.pciMemorySpaceEnabled || !p.pciBusMasterEnabled) {
    return fail(BootPreflightFailure::PciCommandNotReady);
  }
  if (!p.sysmemFlushPageProgrammed) {
    return fail(BootPreflightFailure::SysmemFlushPageNotReady);
  }
  if (!p.allSystemDmaResolved) return fail(BootPreflightFailure::SystemDmaNotReady);
  if (!p.resolvedArtifactsBuilt) return fail(BootPreflightFailure::ArtifactsNotReady);
  if (!p.frtsFramebufferImageVerified) {
    return fail(BootPreflightFailure::FrtsImageNotVerified);
  }
  if (!p.sec2FramebufferImageVerified) {
    return fail(BootPreflightFailure::Sec2ImageNotVerified);
  }
  if (!p.bootstrapRpcPrefilled) {
    return fail(BootPreflightFailure::BootstrapRpcNotPrefilled);
  }
  if (!p.falconPlansPolicyValid) {
    return fail(BootPreflightFailure::FalconPolicyNotReady);
  }
  if (p.wpr2AddrHi != 0u) return fail(BootPreflightFailure::Wpr2AlreadyActive);

  out.ready = true;
  out.coldRollbackStillSafe = true;
  out.firstFailure = BootPreflightFailure::None;
  return out;
}

} // namespace rtxmac::nvidia::gsp
