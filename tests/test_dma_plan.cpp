#include "rtxmac/dma_plan.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

int main() {
  using namespace rtxmac;

  {
    const auto plan = PlanConservativeDmaChunks(0x81000);
    assert(plan.status == DmaPlanStatus::Ok);
    assert(plan.chunks.size() == 5);
    assert(plan.chunks[0].offset == 0x00000 && plan.chunks[0].length == 0x20000);
    assert(plan.chunks[1].offset == 0x20000 && plan.chunks[1].length == 0x20000);
    assert(plan.chunks[4].offset == 0x80000 && plan.chunks[4].length == 0x01000);
  }

  {
    const auto plan = PlanConservativeDmaChunks(63ull * 1024ull * 1024ull);
    assert(plan.status == DmaPlanStatus::Ok);
    assert(!plan.chunks.empty());
    std::uint64_t covered = 0;
    for (const auto& chunk : plan.chunks) {
      assert(chunk.offset == covered);
      assert(chunk.length <= 32ull * 4096ull);
      covered += chunk.length;
    }
    assert(covered == 63ull * 1024ull * 1024ull);
  }

  {
    const auto empty = PlanConservativeDmaChunks(0);
    assert(empty.status == DmaPlanStatus::Ok);
    assert(empty.chunks.empty());
  }

  {
    const auto bad = PlanConservativeDmaChunks(4096, 0, 32);
    assert(bad.status == DmaPlanStatus::InvalidPageSize);
  }

  {
    const auto overflow = PlanConservativeDmaChunks(
        1, std::numeric_limits<std::uint64_t>::max(), 32);
    assert(overflow.status == DmaPlanStatus::SizeOverflow);
  }

  std::cout << "rtxmac conservative DriverKit DMA chunk planner tests passed\n";
  return 0;
}
