#include "rtxmac/sysmem_flush.hpp"

namespace rtxmac::nvidia {

SysmemFlushPagePlan PlanSysmemFlushPage(
    std::uint64_t dmaAddress,
    std::uint32_t currentLo,
    std::uint32_t currentHi) noexcept {
  SysmemFlushPagePlan out{};
  out.dmaAddress = dmaAddress;
  out.oldLo = currentLo;
  out.oldHi = currentHi;

  // A real flush backing allocation is one page and DriverKit DMA allocations
  // are page based. Requiring 4 KiB alignment is stricter than the register's
  // 256-byte granularity and avoids silently truncating a caller address.
  if (dmaAddress == 0u || (dmaAddress % kSysmemFlushPageBytes) != 0u) {
    return out;
  }

  out.newLo = static_cast<std::uint32_t>((dmaAddress >> 8u) & 0xFFFFFFFFull);
  const auto encodedHi = static_cast<std::uint32_t>((dmaAddress >> 40u) & kSysmemFlushAddrHiMask);
  out.newHi = (currentHi & ~kSysmemFlushAddrHiMask) | encodedHi;
  out.valid = true;
  return out;
}

bool ValidateSysmemFlushPagePlan(const SysmemFlushPagePlan& plan) noexcept {
  if (!plan.valid || plan.dmaAddress == 0u ||
      (plan.dmaAddress % kSysmemFlushPageBytes) != 0u) {
    return false;
  }

  const auto expectedLo =
      static_cast<std::uint32_t>((plan.dmaAddress >> 8u) & 0xFFFFFFFFull);
  const auto encodedHi =
      static_cast<std::uint32_t>((plan.dmaAddress >> 40u) & kSysmemFlushAddrHiMask);
  const auto expectedHi = (plan.oldHi & ~kSysmemFlushAddrHiMask) | encodedHi;

  if (plan.newLo != expectedLo || plan.newHi != expectedHi) return false;
  if (((plan.oldHi ^ plan.newHi) & ~kSysmemFlushAddrHiMask) != 0u) return false;
  if (DecodeSysmemFlushPageAddress(plan.newLo, plan.newHi) != plan.dmaAddress) {
    return false;
  }
  return true;
}

} // namespace rtxmac::nvidia
