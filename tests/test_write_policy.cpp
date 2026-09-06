#include "rtxmac/write_policy.hpp"

#include <array>
#include <cassert>
#include <iostream>

int main() {
  using namespace rtxmac;

  constexpr std::array rules{
      MmioWriteRule{.offset = 0x100, .writableMask = 0x0000000Fu},
      MmioWriteRule{.offset = 0x200, .writableMask = 0xFFFFFFFFu},
  };

  static_assert(CheckMmioWrite(rules, 0x100, 0xA0u, 0xA5u) == WriteDecision::Allowed);
  static_assert(CheckMmioWrite(rules, 0x100, 0xA0u, 0xB0u) == WriteDecision::DeniedBitOutsideMask);
  static_assert(CheckMmioWrite(rules, 0x999, 0u, 1u) == WriteDecision::DeniedUnknownRegister);

  DryRunWritePlan plan(rules);
  assert(plan.Add(0x100, 0xA0u, 0xA5u) == WriteDecision::Allowed);
  assert(plan.AllAllowed());
  assert(plan.Add(0x999, 0u, 1u) == WriteDecision::DeniedUnknownRegister);
  assert(!plan.AllAllowed());
  assert(plan.Writes().size() == 2);

  // An empty policy denies every hardware write by default.
  constexpr std::array<MmioWriteRule, 0> emptyRules{};
  DryRunWritePlan lockedDown(emptyRules);
  assert(lockedDown.Add(0x100, 0u, 1u) == WriteDecision::DeniedUnknownRegister);
  assert(!lockedDown.AllAllowed());

  // PRAMIN-like data apertures are represented as a bounded region. A write
  // wholly inside the region is accepted; boundary crossing and unaligned
  // transfers are rejected without creating a BAR-wide exception.
  constexpr std::array regionRules{
      MmioWriteRegionRule{.offset = 0x700000u, .length = 0x100000u, .alignment = 4u},
  };
  static_assert(CheckMmioRegionWrite(regionRules, 0x700000u, 4u) == RegionWriteDecision::Allowed);
  static_assert(CheckMmioRegionWrite(regionRules, 0x7FF000u, 0x1000u) == RegionWriteDecision::Allowed);
  static_assert(CheckMmioRegionWrite(regionRules, 0x7FF000u, 0x1004u) == RegionWriteDecision::DeniedUnknownRange);
  static_assert(CheckMmioRegionWrite(regionRules, 0x700002u, 4u) == RegionWriteDecision::DeniedUnaligned);
  static_assert(CheckMmioRegionWrite(regionRules, 0x700000u, 6u) == RegionWriteDecision::DeniedUnaligned);
  static_assert(CheckMmioRegionWrite(regionRules, 0x700000u, 0u) == RegionWriteDecision::DeniedEmpty);

  DryRunRegionWritePlan regionPlan(regionRules);
  assert(regionPlan.Add(0x745000u, 0x2000u) == RegionWriteDecision::Allowed);
  assert(regionPlan.AllAllowed());
  assert(regionPlan.Add(0x6FF000u, 0x2000u) == RegionWriteDecision::DeniedUnknownRange);
  assert(!regionPlan.AllAllowed());
  assert(regionPlan.Writes().size() == 2u);

  constexpr std::array<MmioWriteRegionRule, 0> emptyRegionRules{};
  DryRunRegionWritePlan lockedRegion(emptyRegionRules);
  assert(lockedRegion.Add(0x700000u, 4u) == RegionWriteDecision::DeniedUnknownRange);
  assert(!lockedRegion.AllAllowed());

  std::cout << "rtxmac dry-run MMIO register/region write policy tests passed\n";
  return 0;
}
