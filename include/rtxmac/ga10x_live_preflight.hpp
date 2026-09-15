#pragma once

#include "rtxmac/boot_package.hpp"
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

// Minimal live-preflight view of the offline GA10x layout. Keeping this
// separate from BootManifest lets DriverKit validate the reserved VRAM boundary
// without linking the full GSP boot-planning dependency graph.
struct ReservedBoundaryProfile {
  bool valid{};
  std::uint64_t vgaWorkspaceOffset{};
  std::uint64_t vbiosReservedOffset{};
};

struct ReservedBoundaryDecision {
  ReservedBoundaryStatus status{ReservedBoundaryStatus::InvalidProfile};
  bool activeMmuLock{};
  std::uint64_t prototypeBoundary{};
  std::uint64_t effectiveBoundary{};
  std::uint64_t mmuLockLow{};
  std::uint64_t mmuLockHigh{};
};

// Build only the boundary values needed by the read-only live check. This does
// not construct a BootManifest and performs no hardware access.
[[nodiscard]] ReservedBoundaryProfile BuildReservedBoundaryProfile(
    const package::PackageView& package) noexcept;

[[nodiscard]] ReservedBoundaryDecision CheckReservedBoundary(
    const ReservedBoundaryProfile& profile,
    const std::optional<MmuLockState>& mmuLock) noexcept;

// Compatibility overload for existing offline callers/tests that already have
// the full prototype profile.
[[nodiscard]] ReservedBoundaryDecision CheckReservedBoundary(
    const Profile& profile,
    const std::optional<MmuLockState>& mmuLock) noexcept;

[[nodiscard]] const char* ReservedBoundaryStatusName(
    ReservedBoundaryStatus status) noexcept;

} // namespace rtxmac::nvidia::prototype
