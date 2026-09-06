#pragma once

#include <cstdint>

namespace rtxmac::nvidia::gsp {

enum class BootPreflightFailure : std::uint8_t {
  None = 0,
  ExecutionGateDisabled,
  PciCommandNotReady,
  SysmemFlushPageNotReady,
  SystemDmaNotReady,
  ArtifactsNotReady,
  FrtsImageNotVerified,
  Sec2ImageNotVerified,
  BootstrapRpcNotPrefilled,
  FalconPolicyNotReady,
  Wpr2AlreadyActive,
};

struct BootCommitPrerequisites {
  // The actual live execution switch remains explicit and defaults false in the
  // caller. Planning/testing can populate every other field without crossing
  // the first Falcon-reset commit boundary.
  bool executionGateEnabled{};
  bool pciMemorySpaceEnabled{};
  bool pciBusMasterEnabled{};
  bool sysmemFlushPageProgrammed{};
  bool allSystemDmaResolved{};
  bool resolvedArtifactsBuilt{};
  bool frtsFramebufferImageVerified{};
  bool sec2FramebufferImageVerified{};
  bool bootstrapRpcPrefilled{};
  bool falconPlansPolicyValid{};
  // Existing GA102 boot planning already uses NV_PFB_PRI_MMU_WPR2_ADDR_HI
  // (BAR0+0x1fa828) as the WPR2-up indication. It must be zero before a fresh
  // bootstrap attempt; a non-zero value is treated as reset-required state.
  std::uint32_t wpr2AddrHi{};
};

struct BootPreflightReport {
  bool ready{};
  bool coldRollbackStillSafe{true};
  BootPreflightFailure firstFailure{BootPreflightFailure::None};
};

// Final software gate immediately before ResetGspForFrts. It deliberately
// accepts only already-verified aggregate facts; it performs no MMIO itself.
[[nodiscard]] BootPreflightReport CheckBootCommitPreflight(
    const BootCommitPrerequisites& prerequisites) noexcept;

} // namespace rtxmac::nvidia::gsp
