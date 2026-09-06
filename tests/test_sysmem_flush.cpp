#include "rtxmac/sysmem_flush.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
  using namespace rtxmac::nvidia;

  constexpr std::uint64_t address = 0x0000001234500000ull;
  constexpr std::uint32_t oldLo = 0xA5A5A5A5u;
  constexpr std::uint32_t oldHi = 0x5A000011u;

  const auto plan = PlanSysmemFlushPage(address, oldLo, oldHi);
  assert(plan.valid);
  assert(plan.oldLo == oldLo);
  assert(plan.oldHi == oldHi);
  assert(plan.newLo == static_cast<std::uint32_t>(address >> 8u));
  assert((plan.newHi & 0xFF000000u) == (oldHi & 0xFF000000u));
  assert(DecodeSysmemFlushPageAddress(plan.newLo, plan.newHi) == address);
  assert(ValidateSysmemFlushPagePlan(plan));

  // No silent truncation of addresses that the register cannot represent
  // exactly at the page granularity expected by the Falcon reset handshake.
  assert(!PlanSysmemFlushPage(address + 1u, oldLo, oldHi).valid);
  assert(!PlanSysmemFlushPage(0u, oldLo, oldHi).valid);

  auto tamperedHi = plan;
  tamperedHi.newHi ^= 0x80000000u; // outside documented addr[63:40]
  assert(!ValidateSysmemFlushPagePlan(tamperedHi));

  auto tamperedLo = plan;
  tamperedLo.newLo ^= 0x100u;
  assert(!ValidateSysmemFlushPagePlan(tamperedLo));

  auto tamperedAddress = plan;
  tamperedAddress.dmaAddress += kSysmemFlushPageBytes;
  assert(!ValidateSysmemFlushPagePlan(tamperedAddress));

  std::cout << "rtxmac GA102 sysmem-flush page tests passed\n";
  return 0;
}
