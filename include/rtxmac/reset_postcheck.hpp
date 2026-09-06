#pragma once

#include <cstdint>

namespace rtxmac::nvidia::gsp {

enum class ResetPostcheckFailure : std::uint8_t {
  None = 0,
  ResetDidNotSucceed,
  PciIdentityUnavailable,
  PciIdentityChanged,
  Bar0Unavailable,
  Wpr2StillActive,
};

struct ResetPostcheckInputs {
  bool resetSucceeded{};
  bool pciIdentityReadable{};
  std::uint16_t expectedVendor{};
  std::uint16_t expectedDevice{};
  std::uint16_t liveVendor{};
  std::uint16_t liveDevice{};
  bool bar0Accessible{};
  std::uint32_t wpr2AddrHi{};
};

struct ResetPostcheckReport {
  bool recovered{};
  bool mayReleasePinnedBootBuffers{};
  bool mayBeginColdRebuild{};
  bool keepPinnedAndRequireReset{};
  ResetPostcheckFailure firstFailure{ResetPostcheckFailure::None};
};

// Validate the minimum observable state required after a recovery reset. The
// old failed-boot DMA/flush backing remains pinned until this reports recovered.
[[nodiscard]] ResetPostcheckReport CheckResetPostconditions(
    const ResetPostcheckInputs& inputs) noexcept;

} // namespace rtxmac::nvidia::gsp
