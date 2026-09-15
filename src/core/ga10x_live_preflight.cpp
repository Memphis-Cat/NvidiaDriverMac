#include "rtxmac/ga10x_live_preflight.hpp"

#include <algorithm>

namespace rtxmac::nvidia::prototype {

ReservedBoundaryDecision CheckReservedBoundary(
    const Profile& profile,
    const std::optional<MmuLockState>& mmuLock) noexcept {
  ReservedBoundaryDecision out{};
  if (profile.status != ProfileStatus::Ok || !profile.manifest.valid) {
    out.status = ReservedBoundaryStatus::InvalidProfile;
    return out;
  }

  out.prototypeBoundary = profile.manifestInputs.vbiosReservedOffset;
  out.effectiveBoundary = out.prototypeBoundary;

  if (!mmuLock.has_value()) {
    out.status = ReservedBoundaryStatus::MmuLockUnavailable;
    return out;
  }
  out.mmuLockLow = mmuLock->low;
  out.mmuLockHigh = mmuLock->high;
  if (!mmuLock->readable) {
    out.status = ReservedBoundaryStatus::MmuLockUnreadable;
    return out;
  }

  if (!mmuLock->valid) {
    out.activeMmuLock = false;
    out.status = ReservedBoundaryStatus::Ok;
    return out;
  }

  out.activeMmuLock = true;
  out.effectiveBoundary = std::min(mmuLock->low, profile.manifestInputs.vgaWorkspaceOffset);
  if (out.effectiveBoundary != profile.manifestInputs.vbiosReservedOffset) {
    out.status = ReservedBoundaryStatus::RebuildRequired;
    return out;
  }

  out.status = ReservedBoundaryStatus::Ok;
  return out;
}

const char* ReservedBoundaryStatusName(ReservedBoundaryStatus status) noexcept {
  switch (status) {
    case ReservedBoundaryStatus::Ok: return "ok";
    case ReservedBoundaryStatus::InvalidProfile: return "invalid-profile";
    case ReservedBoundaryStatus::MmuLockUnavailable: return "mmu-lock-unavailable";
    case ReservedBoundaryStatus::MmuLockUnreadable: return "mmu-lock-unreadable";
    case ReservedBoundaryStatus::RebuildRequired: return "rebuild-required";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::prototype
