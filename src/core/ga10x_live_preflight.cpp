#include "rtxmac/ga10x_live_preflight.hpp"

#include "rtxmac/pci_identity.hpp"

#include <algorithm>

namespace rtxmac::nvidia::prototype {

ReservedBoundaryProfile BuildReservedBoundaryProfile(
    const package::PackageView& packageView) noexcept {
  ReservedBoundaryProfile out{};
  if (packageView.status != package::ParseStatus::Ok ||
      !rtxmac::IsKnownRtx3060Ti(packageView.metadata.pci) ||
      packageView.metadata.vramBytes <= kVgaWorkspaceBytes) {
    return out;
  }

  const std::uint64_t boundary =
      packageView.metadata.vramBytes - kVgaWorkspaceBytes;
  out.valid = true;
  out.vgaWorkspaceOffset = boundary;
  out.vbiosReservedOffset = boundary;
  return out;
}

ReservedBoundaryDecision CheckReservedBoundary(
    const ReservedBoundaryProfile& profile,
    const std::optional<MmuLockState>& mmuLock) noexcept {
  ReservedBoundaryDecision out{};
  if (!profile.valid || profile.vbiosReservedOffset == 0u ||
      profile.vgaWorkspaceOffset == 0u) {
    out.status = ReservedBoundaryStatus::InvalidProfile;
    return out;
  }

  out.prototypeBoundary = profile.vbiosReservedOffset;
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
  out.effectiveBoundary = std::min(mmuLock->low, profile.vgaWorkspaceOffset);
  if (out.effectiveBoundary != profile.vbiosReservedOffset) {
    out.status = ReservedBoundaryStatus::RebuildRequired;
    return out;
  }

  out.status = ReservedBoundaryStatus::Ok;
  return out;
}

ReservedBoundaryDecision CheckReservedBoundary(
    const Profile& profile,
    const std::optional<MmuLockState>& mmuLock) noexcept {
  const ReservedBoundaryProfile boundaryProfile{
      .valid = profile.status == ProfileStatus::Ok && profile.manifest.valid,
      .vgaWorkspaceOffset = profile.manifestInputs.vgaWorkspaceOffset,
      .vbiosReservedOffset = profile.manifestInputs.vbiosReservedOffset,
  };
  return CheckReservedBoundary(boundaryProfile, mmuLock);
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
