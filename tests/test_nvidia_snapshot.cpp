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

  // GA10x MMU lock: PLM bit 0 permits read; address fields are bits 31:4
  // with 4 KiB alignment. These encode [0x1FF000000, 0x1FFFFF000].
  dev.regs[0x001FA7C8u] = 0x00000001u;
  dev.regs[0x001FA82Cu] = 0x01FF0000u;
  dev.regs[0x001FA830u] = 0x01FFFFF0u;

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

  const auto lock = MmuLock(snapshot);
  assert(lock.has_value());
  assert(lock->readable);
  assert(lock->valid);
  assert(lock->low == 0x1FF000000ull);
  assert(lock->high == 0x1FFFFF000ull);

  const auto denied = DecodeMmuLock(0u, 0x01FF0000u, 0x01FFFFF0u);
  assert(!denied.readable);
  assert(!denied.valid);

  const auto inverted = DecodeMmuLock(1u, 0x01FFFFF0u, 0x01FF0000u);
  assert(inverted.readable);
  assert(!inverted.valid);

  dev.regs.erase(0x00840044u);
  const auto partial = CaptureDiagnosticSnapshot(dev);
  assert(!partial.Complete());
  const auto* sec2mb1 = partial.Find(SnapshotRegisterId::Sec2Mailbox1);
  assert(sec2mb1 != nullptr);
  assert(!sec2mb1->Ok());

  std::cout << "rtxmac NVIDIA diagnostic snapshot tests passed\n";
  return 0;
}
