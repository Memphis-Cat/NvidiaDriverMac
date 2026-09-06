#include "rtxmac/reset_postcheck.hpp"

#include <cassert>
#include <iostream>

int main() {
  using namespace rtxmac::nvidia::gsp;

  ResetPostcheckInputs good{
      .resetSucceeded = true,
      .pciIdentityReadable = true,
      .expectedVendor = 0x10DEu,
      .expectedDevice = 0x2489u,
      .liveVendor = 0x10DEu,
      .liveDevice = 0x2489u,
      .bar0Accessible = true,
      .wpr2AddrHi = 0u,
  };

  const auto recovered = CheckResetPostconditions(good);
  assert(recovered.recovered);
  assert(recovered.mayReleasePinnedBootBuffers);
  assert(recovered.mayBeginColdRebuild);
  assert(!recovered.keepPinnedAndRequireReset);

  auto noReset = good;
  noReset.resetSucceeded = false;
  assert(CheckResetPostconditions(noReset).firstFailure ==
         ResetPostcheckFailure::ResetDidNotSucceed);

  auto changed = good;
  changed.liveDevice ^= 1u;
  const auto changedReport = CheckResetPostconditions(changed);
  assert(!changedReport.recovered);
  assert(changedReport.keepPinnedAndRequireReset);
  assert(!changedReport.mayReleasePinnedBootBuffers);
  assert(changedReport.firstFailure == ResetPostcheckFailure::PciIdentityChanged);

  auto noBar = good;
  noBar.bar0Accessible = false;
  assert(CheckResetPostconditions(noBar).firstFailure ==
         ResetPostcheckFailure::Bar0Unavailable);

  auto wprStillUp = good;
  wprStillUp.wpr2AddrHi = 1u;
  const auto wprReport = CheckResetPostconditions(wprStillUp);
  assert(!wprReport.recovered);
  assert(wprReport.keepPinnedAndRequireReset);
  assert(wprReport.firstFailure == ResetPostcheckFailure::Wpr2StillActive);

  std::cout << "rtxmac post-reset GPU recovery tests passed\n";
  return 0;
}
