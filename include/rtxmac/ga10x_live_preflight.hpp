#pragma once

#include "rtxmac/ga10x_prototype.hpp"
#include "rtxmac/nvidia_snapshot.hpp"

#include <cstdint>
#include <optional>

namespace rtxmac::nvidia::prototype {

enum class ReservedBoundaryStatus : std::uint8_t {
  Ok = 0,
  InvalidProfile,
  MmuLockUnavailable,
  MmuLockUnreadable,
  RebuildRequired,
};

struct ReservedBoundaryDecision {
  ReservedBoundaryStatus status{ReservedBoundaryStatus::InvalidProfile};
  bool activeMmuLock{};
  std::uint64_t prototypeBoundary{};
  std::uint64_t effectiveBoundary{};
  std::uint64_t mmuLockLow{};
  std::uint64_t mmuLockHigh{};
};

// Decide whether the offline Tinygrad-compatible WPR/FRTS profile is safe for
// the live GPU. This performs no hardware access itself; it consumes the
// read-only MMU-lock state captured from BAR0.
//
// A valid VBIOS MMU lock below the offline VGA-workspace boundary requires the
// package/profile to be rebuilt. We deliberately do not adjust only WPR meta:
// the FRTS command embedded in FWSEC was also patched offline from that layout.
[[nodiscard]] ReservedBoundaryDecision CheckReservedBoundary(
    const Profile& profile,
    const std::optional<MmuLockState>& mmuLock) noexcept;

[[nodiscard]] const char* ReservedBoundaryStatusName(
    ReservedBoundaryStatus status) noexcept;

} // namespace rtxmac::nvidia::prototype
