#include "rtxmac/reset_postcheck.hpp"

namespace rtxmac::nvidia::gsp {

ResetPostcheckReport CheckResetPostconditions(
    const ResetPostcheckInputs& in) noexcept {
  ResetPostcheckReport out{};
  const auto fail = [&](ResetPostcheckFailure failure) noexcept {
    out.firstFailure = failure;
    out.keepPinnedAndRequireReset = true;
    return out;
  };

  if (!in.resetSucceeded) return fail(ResetPostcheckFailure::ResetDidNotSucceed);
  if (!in.pciIdentityReadable) return fail(ResetPostcheckFailure::PciIdentityUnavailable);
  if (in.expectedVendor == 0u || in.expectedDevice == 0u ||
      in.liveVendor != in.expectedVendor || in.liveDevice != in.expectedDevice) {
    return fail(ResetPostcheckFailure::PciIdentityChanged);
  }
  if (!in.bar0Accessible) return fail(ResetPostcheckFailure::Bar0Unavailable);
  if (in.wpr2AddrHi != 0u) return fail(ResetPostcheckFailure::Wpr2StillActive);

  out.recovered = true;
  out.mayReleasePinnedBootBuffers = true;
  out.mayBeginColdRebuild = true;
  out.keepPinnedAndRequireReset = false;
  out.firstFailure = ResetPostcheckFailure::None;
  return out;
}

} // namespace rtxmac::nvidia::gsp
