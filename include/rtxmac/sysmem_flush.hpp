#pragma once

#include <cstdint>

namespace rtxmac::nvidia {

inline constexpr std::uint32_t kSysmemFlushAddrLoOffset = 0x00100C10u;
inline constexpr std::uint32_t kSysmemFlushAddrHiOffset = 0x00100C40u;
inline constexpr std::uint32_t kSysmemFlushAddrHiMask = 0x00FFFFFFu;
inline constexpr std::uint64_t kSysmemFlushPageBytes = 0x1000ull;

struct SysmemFlushPagePlan {
  bool valid{};
  std::uint64_t dmaAddress{};
  std::uint32_t oldLo{};
  std::uint32_t oldHi{};
  std::uint32_t newLo{};
  std::uint32_t newHi{};
};

// GA102 uses the GA100 NISO sysmem-flush address encoding. The page is a
// prerequisite for Falcon reset handshakes: addr[39:8] is written to 0x100c10
// and addr[63:40] to bits [23:0] of 0x100c40. The caller supplies the live
// register values so unrelated/reserved high-register bits are preserved.
[[nodiscard]] SysmemFlushPagePlan PlanSysmemFlushPage(
    std::uint64_t dmaAddress,
    std::uint32_t currentLo,
    std::uint32_t currentHi) noexcept;

// Re-check a plan before hardware execution. This detects stale/tampered plans
// and guarantees that only the documented address fields may change.
[[nodiscard]] bool ValidateSysmemFlushPagePlan(
    const SysmemFlushPagePlan& plan) noexcept;

[[nodiscard]] constexpr std::uint64_t DecodeSysmemFlushPageAddress(
    std::uint32_t lo,
    std::uint32_t hi) noexcept {
  return (static_cast<std::uint64_t>(lo) << 8u) |
         (static_cast<std::uint64_t>(hi & kSysmemFlushAddrHiMask) << 40u);
}

} // namespace rtxmac::nvidia
