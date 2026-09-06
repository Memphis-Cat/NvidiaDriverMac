#pragma once

#include <cstdint>

namespace rtxmac {

inline constexpr std::uint16_t kPciCommandIoSpace = 1u << 0u;
inline constexpr std::uint16_t kPciCommandMemorySpace = 1u << 1u;
inline constexpr std::uint16_t kPciCommandBusMaster = 1u << 2u;
inline constexpr std::uint16_t kPciDmaWritableMask =
    kPciCommandMemorySpace | kPciCommandBusMaster;

enum class PciCommandPlanStatus : std::uint8_t {
  Ok = 0,
  NoChange,
  InvalidRequestedMask,
};

struct PciCommandTransition {
  PciCommandPlanStatus status{PciCommandPlanStatus::InvalidRequestedMask};
  std::uint16_t oldValue{};
  std::uint16_t newValue{};
  std::uint16_t changedMask{};
  // Exact original value used by a transactional caller to restore state.
  std::uint16_t rollbackValue{};
};

// Produce a non-destructive PCI command transition. requestedSetMask may only
// contain Memory Space and Bus Master; all unrelated command bits are preserved
// exactly and no bit is ever cleared by this enable operation.
[[nodiscard]] constexpr PciCommandTransition PlanPciCommandEnable(
    std::uint16_t current,
    std::uint16_t requestedSetMask = kPciDmaWritableMask) noexcept {
  PciCommandTransition out{};
  out.oldValue = current;
  out.rollbackValue = current;
  if ((requestedSetMask & ~kPciDmaWritableMask) != 0u) {
    out.status = PciCommandPlanStatus::InvalidRequestedMask;
    return out;
  }
  out.newValue = static_cast<std::uint16_t>(current | requestedSetMask);
  out.changedMask = static_cast<std::uint16_t>(current ^ out.newValue);
  out.status = out.changedMask == 0u
      ? PciCommandPlanStatus::NoChange
      : PciCommandPlanStatus::Ok;
  return out;
}

// Validate a precomputed transition before a hardware write. This catches a
// stale plan if the PCI command register changed between planning and execution.
[[nodiscard]] constexpr bool ValidatePciCommandTransition(
    const PciCommandTransition& plan,
    std::uint16_t observedCurrent) noexcept {
  if (plan.status != PciCommandPlanStatus::Ok &&
      plan.status != PciCommandPlanStatus::NoChange) return false;
  if (observedCurrent != plan.oldValue || plan.rollbackValue != plan.oldValue) return false;
  if ((plan.changedMask & ~kPciDmaWritableMask) != 0u) return false;
  if (static_cast<std::uint16_t>(plan.oldValue ^ plan.newValue) != plan.changedMask) return false;
  if ((plan.newValue & ~kPciDmaWritableMask) !=
      (plan.oldValue & ~kPciDmaWritableMask)) return false;
  // Enable plans may set allowed bits but may never clear them.
  if ((plan.oldValue & kPciDmaWritableMask & ~plan.newValue) != 0u) return false;
  return true;
}

} // namespace rtxmac
