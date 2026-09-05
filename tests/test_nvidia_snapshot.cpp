#include "rtxmac/nvidia_snapshot.hpp"

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
    const auto it = regs.find(offset);
    if (it == regs.end()) return {rtxmac::IoStatus::Unmapped, 0u};
    return {rtxmac::IoStatus::Ok, it->second};
  }
};

} // namespace

int main() {
  using namespace rtxmac::nvidia;

  FakeMmio dev;
  for (const auto& reg : kDiagnosticRegisters) dev.regs[reg.offset] = 0u;

  dev.regs[0x00000000u] = 0x174000A1u;
  dev.regs[0x00000A00u] = 0x17412300u;
  dev.regs[0x001183A4u] = 8192u;      // 8192 MiB = 8 GiB
  dev.regs[0x00111388u] = 1u << 7u;   // active=1, halted=0
  dev.regs[0x00118234u] = 0x000000FFu;

  const auto snapshot = CaptureDiagnosticSnapshot(dev);
  assert(snapshot.Complete());
  assert(dev.reads.size() == kDiagnosticRegisters.size());
  for (std::size_t i = 0; i < kDiagnosticRegisters.size(); ++i) {
    assert(dev.reads[i] == kDiagnosticRegisters[i].offset);
  }

  const auto vram = VramSizeBytes(snapshot);
  assert(vram.has_value());
  assert(*vram == (8ull << 30u));

  const auto cpu = GspCpuCtl(snapshot);
  assert(cpu.has_value());
  assert(cpu->active);
  assert(!cpu->halted);

  dev.regs.erase(0x00840044u);
  const auto partial = CaptureDiagnosticSnapshot(dev);
  assert(!partial.Complete());
  const auto* sec2mb1 = partial.Find(SnapshotRegisterId::Sec2Mailbox1);
  assert(sec2mb1 != nullptr);
  assert(!sec2mb1->Ok());

  std::cout << "rtxmac NVIDIA diagnostic snapshot tests passed\n";
  return 0;
}
