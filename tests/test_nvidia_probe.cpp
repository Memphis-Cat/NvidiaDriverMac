#include "rtxmac/nvidia_probe.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

class FakeMmio final : public rtxmac::ReadOnlyMmio {
public:
  std::unordered_map<std::uint32_t, std::uint32_t> regs;
  std::vector<std::uint32_t> reads;

  rtxmac::Read32Result Read32(std::uint32_t offset) override {
    reads.push_back(offset);
    if ((offset & 3u) != 0u) return {rtxmac::IoStatus::Misaligned, 0};
    const auto it = regs.find(offset);
    if (it == regs.end()) return {rtxmac::IoStatus::Unmapped, 0};
    return {rtxmac::IoStatus::Ok, it->second};
  }
};

} // namespace

int main() {
  using namespace rtxmac::nvidia;

  {
    FakeMmio dev;
    dev.regs[kPmcBoot0Offset] = 0x174000A1u;
    dev.regs[kPmcBoot42Offset] = 0x17412300u; // Ampere + implementation 4 = GA104

    const auto probe = ProbeIdentity(dev);
    assert(probe.status == ProbeStatus::Ok);
    assert(probe.boot0 == 0x174000A1u);
    assert(IsAmpere(probe.boot42));
    assert(ChipName(probe.boot42) == "GA104");
    assert((dev.reads == std::vector<std::uint32_t>{kPmcBoot0Offset, kPmcBoot42Offset}));
  }

  {
    FakeMmio dev;
    dev.regs[kPmcBoot0Offset] = 0xFFFFFFFFu;
    dev.regs[kPmcBoot42Offset] = 0xFFFFFFFFu;
    assert(ProbeIdentity(dev).status == ProbeStatus::InvalidAllOnes);
  }

  {
    FakeMmio dev;
    dev.regs[kPmcBoot0Offset] = 0x174000A1u;
    assert(ProbeIdentity(dev).status == ProbeStatus::Boot42ReadFailed);
  }

  {
    FakeMmio dev;
    dev.regs[kPmcBoot0Offset] = 0x194000A1u;
    dev.regs[kPmcBoot42Offset] = 0x19400000u; // Ada architecture, not our first target
    assert(ProbeIdentity(dev).status == ProbeStatus::NotAmpere);
  }

  std::cout << "rtxmac read-only NVIDIA probe tests passed\n";
  return 0;
}
