#include "rtxmac/pramin.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
  using namespace rtxmac::nvidia;

  constexpr std::uint64_t vram8GiB = 8ull << 30u;

  {
    const auto plan = PlanPraminStage(0x12345000ull, 0x2000ull, vram8GiB);
    assert(plan.valid);
    assert(plan.chunks.size() == 1u);
    const auto& c = plan.chunks[0];
    assert(c.vramOffset == 0x12345000ull);
    assert(c.sourceOffset == 0u);
    assert(c.bytes == 0x2000ull);
    assert(c.windowBase == 0x12300000ull);
    assert(c.windowSelector == 0x1230u);
    assert(c.bar0ApertureOffset == 0x745000u);
  }

  {
    // Cross a 1 MiB PRAMIN window boundary.
    const auto plan = PlanPraminStage(0x12FFF000ull, 0x3000ull, vram8GiB);
    assert(plan.valid);
    assert(plan.chunks.size() == 2u);

    const auto& a = plan.chunks[0];
    assert(a.vramOffset == 0x12FFF000ull);
    assert(a.sourceOffset == 0u);
    assert(a.bytes == 0x1000ull);
    assert(a.windowBase == 0x12F00000ull);
    assert(a.windowSelector == 0x12F0u);
    assert(a.bar0ApertureOffset == 0x7FF000u);

    const auto& b = plan.chunks[1];
    assert(b.vramOffset == 0x13000000ull);
    assert(b.sourceOffset == 0x1000ull);
    assert(b.bytes == 0x2000ull);
    assert(b.windowBase == 0x13000000ull);
    assert(b.windowSelector == 0x1300u);
    assert(b.bar0ApertureOffset == 0x700000u);
  }

  {
    // A multi-megabyte image is represented by one chunk per selected window,
    // without generating one action for every 32-bit MMIO store.
    const auto plan = PlanPraminStage(0x1A080000ull, 0x280000ull, vram8GiB);
    assert(plan.valid);
    assert(plan.chunks.size() == 3u);
    assert(plan.chunks[0].bytes == 0x80000ull);
    assert(plan.chunks[1].bytes == 0x100000ull);
    assert(plan.chunks[2].bytes == 0x100000ull);
    assert(plan.chunks[2].sourceOffset == 0x180000ull);
  }

  {
    // Top of an 8 GiB framebuffer remains representable by the selector.
    const auto plan = PlanPraminStage(vram8GiB - 0x1000ull, 0x1000ull, vram8GiB);
    assert(plan.valid);
    assert(plan.chunks.size() == 1u);
    assert(plan.chunks[0].windowBase == 0x1FFF00000ull);
    assert(plan.chunks[0].windowSelector == 0x1FFF0u);
    assert(plan.chunks[0].bar0ApertureOffset == 0x7FF000u);
  }

  assert(!PlanPraminStage(0u, 0u, vram8GiB).valid);
  assert(!PlanPraminStage(vram8GiB, 0x1000ull, vram8GiB).valid);
  assert(!PlanPraminStage(vram8GiB - 0x1000ull, 0x2000ull, vram8GiB).valid);
  assert(!PlanPraminStage(0u, 0x1000ull, 0u).valid);

  std::cout << "rtxmac GA102 PRAMIN VRAM staging planner tests passed\n";
  return 0;
}
