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

  std::cout << "rtxmac dry-run MMIO write policy tests passed\n";
  return 0;
}
