#include "rtxmac/falcon_plan.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
  using namespace rtxmac::nvidia::falcon;

  static_assert(EngineBase(Engine::Gsp) == 0x00110000u);
  static_assert(EngineBase(Engine::Sec2) == 0x00840000u);

  const auto reset = PlanReset(Engine::Gsp, false, 0x174000A1u);
  assert(reset.valid);
  assert(reset.actions.size() >= 8u);
  assert(reset.actions[0].address == 0x001103C0u);
  assert(reset.actions[0].kind == ActionKind::MaskedWrite);
  assert(reset.actions[1].kind == ActionKind::DelayMilliseconds && reset.actions[1].value == 100u);
  assert(reset.actions[3].address == 0x001100F4u);
  assert(reset.actions[4].kind == ActionKind::IfMaskEqualBegin);
  assert(reset.actions[5].address == 0x00111668u);

  HsParameters hs{};
  hs.engine = Engine::Gsp;
  hs.imagePhysicalAddress = 0x20000000ull;
  hs.codeOffset = 0u;
  hs.dataOffset = 0x2000u;
  hs.imemPhysicalBase = 0u;
  hs.imemVirtualBase = 0u;
  hs.imemBytes = 0x200u;
  hs.dmemPhysicalBase = 0u;
  hs.dmemVirtualBase = 0u;
  hs.dmemBytes = 0x100u;
  hs.pkcOffset = 0x80u;
  hs.engineIdMask = 1u;
  hs.ucodeId = 3u;
  hs.mailbox = 0x1122334455667788ull;

  const auto plan = PlanAuthenticatedExecution(hs);
  assert(plan.valid);
  assert(plan.actions[0].address == 0x00110624u); // FBIF_CTL
  assert(plan.actions[1].address == 0x0011010Cu); // DMACTL
  assert(plan.actions[2].address == 0x00110600u); // TRANSCFG0

  const auto countAddress = [&](std::uint32_t address) {
    return std::count_if(plan.actions.begin(), plan.actions.end(),
      [&](const Action& a) { return a.address == address; });
  };
  // 2 IMEM chunks + 1 DMEM chunk -> 3 DMA command writes, plus polls.
  assert(countAddress(0x00110114u) == 3); // DMATRFMOFFS
  assert(countAddress(0x0011011Cu) == 3); // DMATRFFBOFFS
  assert(countAddress(0x00111210u) == 1); // BROM_PARAADDR0
  assert(countAddress(0x00111180u) == 1); // MOD_SEL
  assert(countAddress(0x00110040u) == 1); // mailbox0
  assert(countAddress(0x00110044u) == 1); // mailbox1

  assert(plan.actions[plan.actions.size() - 2].kind == ActionKind::StartCpuRespectAlias);
  assert(plan.actions.back().kind == ActionKind::PollMaskEqual);
  assert(plan.actions.back().address == 0x00110100u);

  hs.imemBytes = 0x180u; // not a multiple of the 256-byte Falcon DMA granule
  assert(!PlanAuthenticatedExecution(hs).valid);

  std::cout << "rtxmac Falcon dry-run planning tests passed\n";
  return 0;
}
